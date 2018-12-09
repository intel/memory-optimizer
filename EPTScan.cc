#include <unistd.h>

#include "EPTScan.h"
#include "lib/debug.h"
#include "lib/stats.h"

extern Option option;

bool EPTScan::should_stop()
{
  if (!option.dram_percent)
    return false;

  // page_refs.get_top_bytes() is 0 when nr_walks == 1
  if (nr_walks <= 2)
    return false;

  unsigned long young_bytes = 0;
  unsigned long top_bytes = 0;
  unsigned long all_bytes = 0;

  gather_walk_stats(young_bytes, top_bytes, all_bytes);

  return 2 * 100 * top_bytes < option.dram_percent * all_bytes;
}

void EPTScan::gather_walk_stats(unsigned long& young_bytes,
                                      unsigned long& top_bytes,
                                      unsigned long& all_bytes)
{
  unsigned long y = 0;
  unsigned long t = 0;
  unsigned long a = 0;

  if (io_error) {
    printdd("gather_walk_stats: skip pid %d\n", pid);
    return;
  }

  for (auto& prc: pagetype_refs) {
    y += prc.page_refs.get_young_bytes();
    t += prc.page_refs.get_top_bytes();
    a += prc.page_refs.size() << prc.page_refs.get_pageshift();
  }

  printdd("pid=%d %lx-%lx top_bytes=%'lu young_bytes=%'lu all_bytes=%'lu\n",
          pid, va_start, va_end, t, y, a);

  young_bytes += y;
  top_bytes += t;
  all_bytes += a;
}

int EPTScan::walk_multi(int nr, float interval)
{
  int err;
  const int max_walks = 30;
  bool auto_stop = false;

  auto maps = proc_maps.load(pid);
  if (maps.empty()) {
    io_error = -ENOENT;
    return -ENOENT;
  }

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
    if (err < 0)
      return err;

    if (auto_stop && should_stop())
      break;

    usleep(interval * 1000000);
  }

  return 0;
}

void EPTScan::prepare_walks(int max_walks)
{
  nr_walks = 0; // for use by count_refs()

  for (int type = 0; type <= MAX_ACCESSED; ++type) {
    auto& prc = pagetype_refs[type];
    prc.page_refs.clear();
    prc.page_refs.set_pageshift(pagetype_shift[type]);
    prc.refs_count.clear();
  }
}

std::vector<unsigned long> EPTScan::sys_refs_count[MAX_ACCESSED + 1];
void EPTScan::reset_sys_refs_count(int nr_walks)
{
  for (auto& src: sys_refs_count) {
    src.clear();
    src.resize(nr_walks + 1, 0);
  }
}

void EPTScan::count_refs_one(ProcIdleRefs& prc)
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
  while (!rc) {
    refs_count[std::min(ref_count, (uint8_t)nr_walks)] += 1;
    rc = prc.page_refs.get_next(addr, ref_count);
  }
}

void EPTScan::count_refs()
{
  if (io_error) {
    printd("count_refs: skip %d\n", pid);
    return;
  }

  for (int type = 0; type <= MAX_ACCESSED; ++type) {
    auto& src = sys_refs_count[type];
    auto& prc = pagetype_refs[type];

    count_refs_one(prc);

    if ((unsigned long)nr_walks + 1 != prc.refs_count.size())
      fprintf(stderr, "ERROR: nr_walks mismatch: %d %lu\n",
              nr_walks, prc.refs_count.size());

    for (int i = 0; i <= nr_walks; ++i) {
      src[i] += prc.refs_count[i];
    }
  }
}

int EPTScan::save_counts(std::string filename)
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

  fprintf(file, "Scan result: memory (KB) group by reference count\n");
  fprintf(file, "%4s %15s %15s %15s\n",
                "refs",
                "4k_page",
                "2M_page",
                "1G_page");
  fprintf(file, "======================================================\n");

  unsigned long sum_kb[IDLE_PAGE_TYPE_MAX] = {};
  int nr = sys_refs_count[PTE_ACCESSED].size();

  for (int i = 0; i < nr; i++) {
    fprintf(file, "%4d", i);
    for (const int& type: {PTE_ACCESSED, PMD_ACCESSED, PUD_PRESENT}) {
      unsigned long pages = sys_refs_count[type][i];
      unsigned long kb = pages * (pagetype_size[type] >> 10);
      fprintf(file, " %'15lu", kb);
      sum_kb[type] += kb;
    }
    fprintf(file, "\n");
  }

  fprintf(file, "SUM ");
  unsigned long total_kb = 0;
  for (const int& type: {PTE_ACCESSED, PMD_ACCESSED, PUD_PRESENT}) {
    unsigned long kb = sum_kb[type];
    fprintf(file, " %'15lu", kb);
    total_kb += kb;
  }
  fprintf(file, "\nALL  %'15lu\n", total_kb);

  if (file != stdout)
    fclose(file);

  return err;
}

