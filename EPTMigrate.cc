#include <stdio.h>
#include <ctype.h>

#include <map>
#include <string>
#include <iostream>
#include <algorithm>
#include <sys/mman.h>

#include <numa.h>
#include <numaif.h>
#include "Option.h"
#include "EPTMigrate.h"
#include "lib/debug.h"
#include "lib/stats.h"
#include "AddrSequence.h"
#include "VMAInspect.h"
#include "Numa.h"

#define MPOL_MF_SW_YOUNG (1<<7)

extern Option option;
using namespace std;

MigrateStats EPTMigrate::sys_migrate_stats;

void MigrateStats::clear()
{
  MoveStats::clear();
  anon_kb = 0;
}

void MigrateStats::add(MigrateStats* s)
{
  MoveStats::add(s);
  anon_kb += s->anon_kb;
}

void MigrateStats::show(Formatter& fmt, MigrateWhat mwhat)
{
  const char *type = (mwhat == MIGRATE_HOT ? "hot" : "cold");
  const char *node = (mwhat == MIGRATE_HOT ? "DRAM" : "PMEM");

  fmt.print("\n");
  fmt.print("find %4s pages: %'15lu %3d%% of anon pages\n", type, to_move_kb, percent(to_move_kb, anon_kb));
  fmt.print("already in %4s: %'15lu %3d%% of %4s pages\n", node, skip_kb, percent(skip_kb, to_move_kb), type);
  fmt.print("need to migrate: %'15lu %3d%% of %4s pages\n", move_kb, percent(move_kb, to_move_kb), type);
}

void EPTMigrate::reset_sys_migrate_stats()
{
  sys_migrate_stats.clear();
}

void EPTMigrate::count_migrate_stats()
{
  sys_migrate_stats.add(&migrate_stats);
}

EPTMigrate::EPTMigrate() : numa_collection(NULL)
{
  // inherit from global settings
  policy.migrate_what = option.migrate_what;
  policy.dump_distribution = false;
}


size_t EPTMigrate::get_threshold_refs(ProcIdlePageType type,
                                      int& min_refs, int& max_refs)
{
  int nr_walks = get_nr_walks();

  if (type & PAGE_ACCESSED_MASK && option.nr_walks == 0) {
    min_refs = nr_walks;
    max_refs = nr_walks;
    return 0;
  }
  if (type & PAGE_ACCESSED_MASK && option.hot_min_refs > 0) {
    min_refs = option.hot_min_refs;
    max_refs = nr_walks;
    return 0;
  }
  if (!(type & PAGE_ACCESSED_MASK) && option.cold_max_refs >= 0) {
    min_refs = 0;
    max_refs = option.cold_max_refs;
    return 0;
  }

  const AddrSequence& page_refs = get_pagetype_refs(type).page_refs;
  vector<unsigned long> refs_count = get_pagetype_refs(type).refs_count;

  double ratio;

  if (option.dram_percent) {
    if (type & PAGE_ACCESSED_MASK)
      ratio = option.dram_percent / 100.0;
    else
      ratio = (100.0 - option.dram_percent) / 100.0;
  } else {
    ProcVmstat proc_vmstat;
    ratio = (double) calc_numa_anon_capacity(type, proc_vmstat)
                     / proc_vmstat.anon_capacity();
  }

  // XXX: this assumes all processes have same hot/cold distribution
  size_t portion = page_refs.size() * ratio;
  long quota = portion;

  fmt.print("migrate ratio: %.2f = %lu / %lu\n", ratio, portion, page_refs.size());

  if (type & PAGE_ACCESSED_MASK) {
    min_refs = nr_walks;
    max_refs = nr_walks;
    for (; min_refs > 1; --min_refs) {
      quota -= refs_count[min_refs];
      if (quota <= 0)
        break;
    }
    if (min_refs < nr_walks)
      ++min_refs;
  } else {
    min_refs = 0;
    max_refs = 0;
    for (; max_refs < nr_walks / 2; ++max_refs) {
      quota -= refs_count[max_refs];
      if (quota <= 0)
        break;
    }
    max_refs >>= 1;
  }

  fmt.print("refs range: %d-%d\n", min_refs, max_refs);

  return portion;
}

int EPTMigrate::select_top_pages(ProcIdlePageType type)
{
  AddrSequence& page_refs = get_pagetype_refs(type).page_refs;
  int min_refs;
  int max_refs;
  unsigned long addr;
  uint8_t ref_count;
  int iter_ret;

  if (page_refs.empty())
    return 1;

  migrate_stats.anon_kb += page_refs.size() << (page_refs.get_pageshift() - 10);

  get_threshold_refs(type, min_refs, max_refs);

  /*
  for (auto it = page_refs.begin(); it != page_refs.end(); ++it) {
    printdd("va: %lx count: %d\n", it->first, (int)it->second);
    if (it->second >= min_refs &&
        it->second <= max_refs)
      pages_addr[type].push_back((void *)(it->first << PAGE_SHIFT));
  }
  */

  iter_ret = page_refs.get_first(addr, ref_count);
  while (!iter_ret) {
    if (ref_count >= min_refs &&
        ref_count <= max_refs)
      pages_addr[type].push_back((void *)addr);

    iter_ret = page_refs.get_next(addr, ref_count);
  }

  if (pages_addr[type].empty())
    return 1;

  sort(pages_addr[type].begin(), pages_addr[type].end());

  if (debug_level() >= 2)
    for (size_t i = 0; i < pages_addr[type].size(); ++i) {
      cout << "page " << i << ": " << pages_addr[type][i] << endl;
    }

  return 0;
}

int EPTMigrate::migrate()
{
  int err = 0;

  // Assume PLACEMENT_DRAM processes will mlock themselves to LRU_UNEVICTABLE.
  // Just need to skip them in user space migration.
  if (policy.placement == PLACEMENT_DRAM)
    return 0;

  fmt.clear();
  fmt.reserve(1<<10);

  if (policy.migrate_what & MIGRATE_COLD) {
    migrate_stats.clear();
    err = migrate(PTE_IDLE);
    if (err)
      goto out;
    err = migrate(PMD_IDLE);
    if (err)
      goto out;
    migrate_stats.show(fmt, MIGRATE_COLD);
  }

  if (policy.migrate_what & MIGRATE_HOT) {
    migrate_stats.clear();
    err = migrate(PTE_ACCESSED);
    if (err)
      goto out;
    err = migrate(PMD_ACCESSED);
    migrate_stats.show(fmt, MIGRATE_HOT);
  }

  if (policy.dump_distribution) {
    VMAInspect vma_inspector;
    vma_inspector.dump_task_nodes(pid, &fmt);
  }

out:
  if (!fmt.empty())
    std::cout << fmt.str();

  return err;
}

int EPTMigrate::migrate(ProcIdlePageType type)
{
  int ret;

  ret = select_top_pages(type);
  if (ret)
    return std::min(ret, 0);

  ret = do_move_pages(type);
  return ret;
}

long EPTMigrate::do_move_pages(ProcIdlePageType type)
{
  auto& addrs = pages_addr[type];
  long ret;

  migrator.set_pid(pid);
  migrator.set_page_shift(pagetype_shift[type]);
  migrator.set_batch_size(1024);
  migrator.set_migration_type(type);

  ret = migrator.locate_move_pages(addrs,
                                   &migrate_stats);

  return ret;
}

unsigned long EPTMigrate::calc_numa_anon_capacity(ProcIdlePageType type,
                                                  ProcVmstat& proc_vmstat)
{
  unsigned long sum;
  NumaNodeCollection::iterator iter, iter_end;

  if (!numa_collection)
    return 0;

  if (type & PAGE_ACCESSED_MASK) {
    iter = numa_collection->dram_begin();
    iter_end = numa_collection->dram_end();
  } else {
    iter = numa_collection->pmem_begin();
    iter_end = numa_collection->pmem_end();
  }

  for(; iter != iter_end; ++iter) {
    sum += proc_vmstat.anon_capacity(iter->id());
  }

  return sum;
}
