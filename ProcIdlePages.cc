#include <fcntl.h>
#include <iostream>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Option.h"
#include "ProcIdlePages.h"
#include "lib/debug.h"
#include "lib/stats.h"

unsigned long pagetype_size[IDLE_PAGE_TYPE_MAX] = {
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

unsigned long pagetype_shift[IDLE_PAGE_TYPE_MAX] = {
	// 4k page
	[PTE_IDLE]      = 12,
	[PTE_ACCESSED]  = 12,

	// 2M page
	[PMD_IDLE]      = 21,
	[PMD_ACCESSED]  = 21,

	// 1G page
	[PUD_IDLE]      = 30,
	[PUD_ACCESSED]  = 30,

	[PTE_HOLE]      = 12,
	[PMD_HOLE]      = 21,
	[PUD_HOLE]      = 30,
	[P4D_HOLE]      = 39,
	[PGDIR_HOLE]    = 39,
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

ProcIdlePages::ProcIdlePages(pid_t n)
  : pid(n)
{
  va_start = 0;
  va_end = TASK_SIZE_MAX;
}

bool ProcIdlePages::should_stop()
{
    if (!option.dram_percent)
      return false;

    // page_refs.get_top_bytes() is 0 when nr_walks == 1
    if (nr_walks <= 2)
      return false;

    unsigned long top_bytes = 0;
    unsigned long all_bytes = 0;

    get_sum(top_bytes, all_bytes);

    return 2 * 100 * top_bytes < option.dram_percent * all_bytes;
}

void ProcIdlePages::get_sum(unsigned long& top_bytes, unsigned long& all_bytes)
{
    for (auto& prc: pagetype_refs) {
      top_bytes += prc.page_refs.get_top_bytes();
      all_bytes += prc.page_refs.size() << prc.page_refs.get_pageshift();
    }
    printdd("top_bytes=%'lu all_bytes=%'lu\n", top_bytes, all_bytes);
}

int ProcIdlePages::walk_multi(int nr, float interval)
{
  int err;
  const int max_walks = 30;
  bool auto_stop = false;

  auto maps = proc_maps.load(pid);
  if (maps.empty())
    return -ENOENT;

  nr_walks = 0; // for use by count_refs()
  if (nr > 0xff) {
    printf("limiting nr_walks to uint8_t size\n");
    nr = 0xff;
  } else if (nr == 0) {
    auto_stop = true;
    nr = max_walks;
  }

  prepare_walks(nr);

  for (int i = 0; i < nr; ++i)
  {
    err = walk();
    if (err)
      return err;

    if (auto_stop && should_stop())
      break;

    usleep(interval * 1000000);
  }

  return 0;
}

void ProcIdlePages::prepare_walks(int max_walks)
{
  for (int type = 0; type <= MAX_ACCESSED; ++type) {
    auto& prc = pagetype_refs[type];
    prc.page_refs.clear();
    prc.page_refs.set_pageshift(pagetype_shift[type]);
    prc.refs_count.clear();
    prc.refs_count.resize(max_walks + 1);
  }
}

int ProcIdlePages::walk_vma(proc_maps_entry& vma)
{
    unsigned long va = vma.start;
    int rc = 0;

    if (va < va_start)
      return 0;

    // skip [vsyscall] etc. special kernel sections
    if (va >= va_end)
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

    ++nr_walks;
    read_buf.resize(READ_BUF_SIZE);

    // must do rewind() before a walk() start.
    for (auto& prc: pagetype_refs)
      prc.page_refs.rewind();

    for (auto &vma: address_map)
      walk_vma(vma);

    close(idle_fd);

    return 0;
}

std::vector<unsigned long> ProcIdlePages::sys_refs_count[MAX_ACCESSED + 1];
void ProcIdlePages::reset_sys_refs_count()
{
  for (auto& src: sys_refs_count)
    src.clear();
}

void ProcIdlePages::count_refs_one(ProcIdleRefs& prc)
{
    int rc;
    unsigned long addr;
    uint8_t ref_count;
    std::vector<unsigned long>& refs_count = prc.refs_count;

    refs_count.clear();
    refs_count.resize(nr_walks + 1, 0);

    // prc.page_refs.smooth_payloads();

    // In the rare case of changed VMAs, their start/end boundary may not align
    // with the underlying huge page size. If the same huge page is covered by
    // 2 VMAs, there will be duplicate accounting for the same page. The easy
    // workaround is to enforce min() check here.
    rc = prc.page_refs.get_first(addr, ref_count);
    while(!rc) {
      refs_count[std::min(ref_count, (uint8_t)nr_walks)] += 1;
      rc = prc.page_refs.get_next(addr, ref_count);
    }
}

void ProcIdlePages::count_refs()
{
  for (int type = 0; type <= MAX_ACCESSED; ++type) {
    auto& src = sys_refs_count[type];
    auto& prc = pagetype_refs[type];

    count_refs_one(prc);

    if (src.size() <= (unsigned long)nr_walks)
      src.resize(nr_walks + 1, 0);
    for (unsigned long i = 0; i < prc.refs_count.size(); ++i)
      src[i] += prc.refs_count[i];
  }
}

int ProcIdlePages::save_counts(std::string filename)
{
  int err = 0;
  FILE *file;
  if (filename.empty())
    file = stdout;
  else
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

  unsigned long sum_kb[IDLE_PAGE_TYPE_MAX] = {};
  int nr = sys_refs_count[PTE_ACCESSED].size();

  for (int i = 0; i <= nr; i++) {
    fprintf(file, "%4d", i);
    for (const int& type: {PTE_ACCESSED, PMD_ACCESSED, PUD_ACCESSED}) {
      unsigned long pages = sys_refs_count[type][i];
      unsigned long kb = pages * (pagetype_size[type] >> 10);
      fprintf(file, " %'15lu", kb);
      sum_kb[type] += kb;
    }
    fprintf(file, "\n");
  }

  fprintf(file, "SUM ");
  unsigned long total_kb = 0;
  for (const int& type: {PTE_ACCESSED, PMD_ACCESSED, PUD_ACCESSED}) {
    unsigned long kb = sum_kb[type];
    fprintf(file, " %'15lu", kb);
    total_kb += kb;
  }
  fprintf(file, "\nALL  %'15lu\n", total_kb);

  if (file != stdout)
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
  unsigned long page_size = pagetype_size[type];
  AddrSequence& page_refs = pagetype_refs[type | PAGE_ACCESSED_MASK].page_refs;

  if (va & (page_size - 1)) {
    printf("ignore unaligned addr: %d %lx+%d %lx\n", type, va, nr, page_size);
    return;
  }

  for (int i = 0; i < nr; ++i)
  {
    if (type & PAGE_ACCESSED_MASK)
      page_refs.inc_payload(va, 1);
    else
      page_refs.inc_payload(va, 0);

    //AddrSequence is not easy to random access, consider move
    //this checking into AddrSequence.
    //if (page_refs[va] > nr_walks)
    //  printf("error counted duplicate va: %lx\n", va);

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

    va &= ~(pagetype_size[type] - 1);

    if (type <= MAX_ACCESSED)
      inc_page_refs(type, nr, va, vma.end);

    va += pagetype_size[type] * nr;
  }
}

unsigned long ProcIdlePages::va_to_offset(unsigned long va)
{
  return va;
}

unsigned long ProcIdlePages::offset_to_va(unsigned long offset)
{
  return offset;
}

void ProcIdlePages::set_va_range(unsigned long start, unsigned long end)
{
  va_start = start;
  va_end = end;
}
