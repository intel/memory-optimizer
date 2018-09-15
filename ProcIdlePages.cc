#include <fcntl.h>
#include <iostream>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ProcIdlePages.h"
#include "lib/debug.h"
#include "lib/stats.h"

static unsigned long pagetype_size[16] = {
	// 4k page
	[PTE_IDLE]      = PAGE_SIZE,
	[PTE_ACCESSED]  = PAGE_SIZE,

	// 2M page
	[PMD_IDLE]      = PMD_SIZE,
	[PMD_ACCESSED]  = PMD_SIZE,

	// 1G page
	[PUD_IDLE]      = PUD_SIZE,
	[PUD_ACCESSED]  = PUD_SIZE,

	[PTE_HOLE]      = PAGE_SIZE,
	[PMD_HOLE]      = PMD_SIZE,
	[PUD_HOLE]      = PUD_SIZE,
	[P4D_HOLE]      = P4D_SIZE,
	[PGDIR_HOLE]    = PGDIR_SIZE,
};

const char* pagetype_name[IDLE_PAGE_TYPE_MAX] = {
  [PTE_IDLE]     = "4K_idle",
  [PTE_ACCESSED] = "4K_accessed",

  [PMD_IDLE]     = "2M_idle",
  [PMD_ACCESSED] = "2M_accessed",

  [PUD_IDLE]     = "1G_idle",
  [PUD_ACCESSED] = "1G_accessed",

  [PTE_HOLE]     = "4K_hole",
  [PMD_HOLE]     = "2M_hole",
  [PUD_HOLE]     = "1G_hole",
  [P4D_HOLE]     = "512G_hole",
  [PGDIR_HOLE]   = "512G_hole",
};

int ProcIdlePages::walk_multi(int nr, float interval)
{
  int err;

  auto maps = proc_maps.load(pid);
  if (maps.empty())
    return -ENOENT;

  nr_walks = nr; // for use by count_refs()
  if (nr_walks > 0xff) {
    printf("limiting nr_walks to uint8_t size\n");
    nr_walks = 0xff;
  }

  for (auto& prc: pagetype_refs) {
    prc.page_refs.clear();
    prc.refs_count.clear();
    prc.refs_count.resize(nr + 1);
  }

  for (int i = 0; i < nr; ++i)
  {
    err = walk();
    if (err)
      return err;

    usleep(interval * 1000000);
  }

  return 0;
}

int ProcIdlePages::walk_vma(proc_maps_entry& vma)
{
    unsigned long va = vma.start;
    int rc = 0;

    // skip [vsyscall] etc. special kernel sections
    if (va > TASK_SIZE_MAX)
      return 0;

    if (!proc_maps.is_anonymous(vma))
      return 0;

    if (debug_level() >= 2)
      proc_maps.show(vma);

    if (lseek(idle_fd, va_to_offset(va), SEEK_SET) == (off_t) -1)
    {
      printf(" error: seek for addr %lx failed, skip.\n", va);
      perror("lseek error");
      return -1;
    }

    for (; va < vma.end;)
    {
      off_t pos = lseek(idle_fd, 0, SEEK_CUR);
      if (pos == (off_t) -1) {
        perror("SEEK_CUR error");
        return -1;
      }
      if ((unsigned long)pos != va) {
        fprintf(stderr, "error pos != va: %lu %lu\n", pos, va);
        return -2;
      }

      rc = read(idle_fd, read_buf.data(), read_buf.size());
      if (rc < 0) {
        if (rc == -ENXIO)
          return 0;
        if (rc == -ERANGE)
          continue;
        perror("read error");
        proc_maps.show(vma);
        return rc;
      }

      if (!rc)
      {
        printf("read 0 size\n");
        return 0;
      }

      parse_idlepages(vma, va, rc);
    }

    return 0;
}

int ProcIdlePages::walk()
{
    std::vector<proc_maps_entry> address_map = proc_maps.load(pid);

    if (address_map.empty())
      return -ESRCH;

    int idle_fd = open_file();
    if (idle_fd < 0)
      return idle_fd;

    read_buf.resize(READ_BUF_SIZE);

    for (auto &vma: address_map)
      walk_vma(vma);

    close(idle_fd);

    return 0;
}

void ProcIdlePages::count_refs_one(ProcIdleRefs& prc)
{
    std::vector<unsigned long>& refs_count = prc.refs_count;

    refs_count.clear();
    refs_count.resize(nr_walks + 1, 0);

    // In the rare case of changed VMAs, their start/end boundary may not align
    // with the underlying huge page size. If the same huge page is covered by
    // 2 VMAs, there will be duplicate accounting for the same page. The easy
    // workaround is to enforce min() check here.
    for(const auto& kv: prc.page_refs)
      refs_count[std::min(kv.second, (uint8_t)nr_walks)] += 1;
}

void ProcIdlePages::count_refs()
{
  for (auto& prc: pagetype_refs)
    count_refs_one(prc);
}

int ProcIdlePages::save_counts(std::string filename)
{
  int err = 0;

  FILE *file;
  file = fopen(filename.c_str(), "w");
  if (!file) {
    std::cerr << "open file " << filename << "failed" << std::endl;
    perror(filename.c_str());
    return -1;
  }

  fprintf(file, "%4s %15s %15s %15s\n",
                "refs",
                "4k_page",
                "2M_page",
                "1G_page");
  fprintf(file, "======================================================\n");

  for (int i = 0; i <= nr_walks; i++) {
    fprintf(file, "%4d", i);
    for (const int& type: {PTE_ACCESSED, PMD_ACCESSED, PUD_ACCESSED}) {
      unsigned long pages = pagetype_refs[type].refs_count[i];
      fprintf(file, " %'15lu", pages * (pagetype_size[type] >> 10));
    }
    fprintf(file, "\n");
  }

  fclose(file);

  return err;
}

int ProcIdlePages::open_file()
{
    char filepath[PATH_MAX];

    memset(filepath, 0, sizeof(filepath));
    snprintf(filepath, sizeof(filepath), "/proc/%d/idle_bitmap", pid);

    idle_fd = open(filepath, O_RDWR);
    if (idle_fd < 0)
      perror(filepath);

    return idle_fd;
}

void ProcIdlePages::inc_page_refs(ProcIdlePageType type, int nr,
                                  unsigned long va, unsigned long end)
{
  page_refs_map& page_refs = pagetype_refs[type | PAGE_ACCESSED_MASK].page_refs;
  unsigned long page_size = pagetype_size[type];

  for (int i = 0; i < nr; ++i)
  {
    unsigned long vpfn = va >> PAGE_SHIFT;

    if (type & PAGE_ACCESSED_MASK)
      inc_count(page_refs, vpfn);
    else {
      auto search = page_refs.find(vpfn);

      if (search == page_refs.end())
        page_refs[vpfn] = 0;
    }

    if (page_refs[vpfn] > nr_walks)
      printf("error counted duplicate vpfn: %lx\n", vpfn);

    va += page_size;
    if (va >= end)
      break;
  }
}

void ProcIdlePages::parse_idlepages(proc_maps_entry& vma,
                                    unsigned long& va,
                                    int bytes)
{
  for (int i = 0; i < bytes; ++i)
  {
    ProcIdlePageType type = (ProcIdlePageType) read_buf[i].type;
    int nr = read_buf[i].nr;

    if (va >= vma.end) {
      // This can happen infrequently when VMA changed. The new pages can be
      // simply ignored -- they arrive too late to have accurate accounting.
      if (debug_level() >= 2) {
        printf("WARNING: va >= end: %lx %lx i=%d bytes=%d type=%d nr=%d\n",
               va, vma.end, i, bytes, type, nr);
        proc_maps.show(vma);
        for (int j = 0; j < bytes; ++j)
          printf("%x:%x  ", read_buf[j].type, read_buf[j].nr);
        puts("");
      }
      return;
    }

    if (type <= MAX_ACCESSED)
      inc_page_refs(type, nr, va, vma.end);

    va += pagetype_size[type] * nr;
  }
}

unsigned long ProcIdlePages::va_to_offset(unsigned long va)
{
  unsigned long offset = va;

  // offset /= PAGE_SIZE;
  offset &= ~(PAGE_SIZE - 1);

  return offset;
}


unsigned long ProcIdlePages::offset_to_va(unsigned long va)
{
  return va;
}
