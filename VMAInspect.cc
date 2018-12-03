#include <sys/user.h>

#include "lib/debug.h"
#include "lib/stats.h"
#include "ProcMaps.h"
#include "Formatter.h"
#include "VMAInspect.h"
#include "Numa.h"

void VMAInspect::fill_addrs(std::vector<void *>& addrs, unsigned long start)
{
    void **p = &addrs[0];
    void **pp = &addrs[addrs.size()-1];

    for (; p <= pp; ++p) {
      *p = (void *)start;
      start += PAGE_SIZE;
    }
}

void VMAInspect::dump_node_percent(int slot)
{
  auto status_count = locator.get_status_count();
  size_t dram_nodes_size = 0;

  if (numa_collection) {
    for (auto iter = numa_collection->dram_begin();
         iter != numa_collection->dram_end(); ++iter) {
      dram_nodes_size += (size_t)status_count[iter->id()];
    }
  }

  int pct = percent(dram_nodes_size, locator.get_status().size());
  fmt->print("%2d %3d%% |", slot, pct);
  for (int i = 0; i < pct; ++i)
    fmt->print("#");
  fmt->print("\n");
}

int VMAInspect::dump_vma_nodes(proc_maps_entry& vma)
{
  unsigned long nr_pages;
  int err = 0;

  if (vma.end - vma.start < 1<<30)
    return 0;

  nr_pages = (vma.end - vma.start) >> PAGE_SHIFT;

  unsigned long total_mb = (vma.end - vma.start) >> 20;
  fmt->print("\nDRAM page distribution across 10 VMA slots: ");
  fmt->print("(pid=%d vma_mb=%'lu)\n", pid, total_mb);

  const int nr_slots = 10;
  unsigned long slot_pages = nr_pages / nr_slots;

  locator.set_pid(pid);

  std::vector<void *> addrs;
  addrs.resize(slot_pages);

  MovePagesStatusCount status_sum;

  for (int i = 0; i < nr_slots; ++i)
  {
    fill_addrs(addrs, vma.start + i * addrs.size() * PAGE_SIZE);

    locator.clear_status_count();
    err = locator.move_pages(addrs, true);
    if (err) {
      perror("move_pages");
      return err;
    }

    locator.calc_status_count();
    dump_node_percent(i);
    locator.add_status_count(status_sum);
  }

  if (!err && debug_level())
    locator.show_status_count(fmt, status_sum);

  return err;
}

int VMAInspect::dump_task_nodes(pid_t i, Formatter* m)
{
  ProcMaps proc_maps;
  int err = 0;

  pid = i;
  fmt = m;

  auto maps = proc_maps.load(pid);

  for (auto &vma: maps) {
    err = dump_vma_nodes(vma);
    if (err)
      break;
  }

  return err;
}
