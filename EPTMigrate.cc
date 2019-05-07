/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 *          Huang Ying <ying.huang@intel.com>
 *          Liu Jingqi <jingqi.liu@intel.com>
 */

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
  if (!to_move_kb)
    return;

  const char *type = (mwhat == MIGRATE_HOT ? "hot" : "cold");
  const char *node = (mwhat == MIGRATE_HOT ? "DRAM" : "PMEM");

  fmt.print("\n");
  fmt.print("find %4s pages: %'15lu %3d%% of anon pages\n", type, to_move_kb, percent(to_move_kb, anon_kb));
  fmt.print("already in %4s: %'15lu %3d%% of %4s pages\n", node, skip_kb, percent(skip_kb, to_move_kb), type);
  fmt.print("need to migrate: %'15lu %3d%% of %4s pages\n", move_kb, percent(move_kb, to_move_kb), type);

  if (option.debug_move_pages)
    show_move_state(fmt);
}

void EPTMigrate::reset_sys_migrate_stats()
{
  sys_migrate_stats.clear();
}

void EPTMigrate::count_migrate_stats()
{
  sys_migrate_stats.add(&migrate_stats);
}

EPTMigrate::EPTMigrate()
{
  // inherit from global settings
  policy.migrate_what = option.migrate_what;
  policy.dump_distribution = false;
}


size_t EPTMigrate::get_threshold_refs(ProcIdlePageType type,
                                      int& min_refs, int& max_refs)
{
  int nr_walks = get_nr_walks();

  if (type <= MAX_ACCESSED && option.nr_walks == 0) {
    min_refs = nr_walks;
    max_refs = nr_walks;
    return 0;
  }
  if (type <= MAX_ACCESSED && option.hot_min_refs > 0) {
    min_refs = option.hot_min_refs;
    max_refs = nr_walks;
    return 0;
  }
  if (!(type <= MAX_ACCESSED) && option.cold_max_refs >= 0) {
    min_refs = 0;
    max_refs = option.cold_max_refs;
    return 0;
  }

  const AddrSequence& page_refs = get_pagetype_refs(type).page_refs;
  const histogram_type& refs_count = get_pagetype_refs(type).histogram_2d[REF_LOC_ALL];
  double ratio;

  if (option.dram_percent) {
    if (type <= MAX_ACCESSED)
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

  if (type <= MAX_ACCESSED) {
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
  std::vector<void *>& addrs = pages_addr[pagetype_index[type]];

  int min_refs;
  int max_refs;
  unsigned long addr;
  uint8_t ref_count;
  int8_t unused_nid;
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
      addrs.push_back((void *)(it->first << PAGE_SHIFT));
  }
  */

  iter_ret = page_refs.get_first(addr, ref_count, unused_nid);
  while (!iter_ret) {
    if (ref_count >= min_refs &&
        ref_count <= max_refs)
      addrs.push_back((void *)addr);

    iter_ret = page_refs.get_next(addr, ref_count, unused_nid);
  }

  if (addrs.empty())
    return 1;

  sort(addrs.begin(), addrs.end());

  if (debug_level() >= 2)
    for (size_t i = 0; i < addrs.size(); ++i) {
      cout << "page " << i << ": " << addrs[i] << endl;
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
    migrate(PTE_IDLE);
    migrate(PMD_IDLE);
    migrate_stats.show(fmt, MIGRATE_COLD);
  }

  if (policy.migrate_what & MIGRATE_HOT) {
    migrate_stats.clear();
    migrate(PTE_ACCESSED);
    migrate(PMD_ACCESSED);
    migrate_stats.show(fmt, MIGRATE_HOT);
  }

  if (policy.dump_distribution && migrate_stats.move_kb) {
    VMAInspect vma_inspector;
    vma_inspector.set_numa_collection(numa_collection);
    vma_inspector.dump_task_nodes(pid, &fmt);
  }

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
  auto& addrs = pages_addr[pagetype_index[type]];
  long ret;

  migrator.set_pid(pid);
  migrator.set_page_shift(pagetype_shift[type]);
  migrator.set_batch_size(pagetype_batchsize[type]);
  migrator.set_migration_type(type);
  migrator.set_numacollection(numa_collection);

  ret = migrator.locate_move_pages(context,
                                   addrs,
                                   &migrate_stats);

  return ret;
}

unsigned long EPTMigrate::calc_numa_anon_capacity(ProcIdlePageType type,
                                                  ProcVmstat& proc_vmstat)
{
  unsigned long sum = 0;
  const std::vector<NumaNode *> *nodes;

  if (!numa_collection)
    return 0;

  if (type <= MAX_ACCESSED)
    nodes = &numa_collection->get_dram_nodes();
  else
    nodes = &numa_collection->get_pmem_nodes();

  for(auto& node: *nodes)
    sum += proc_vmstat.anon_capacity(node->id());

  return sum;
}

int EPTMigrate::promote_and_demote(ProcIdlePageType type,
                                   long nr_promote, long nr_demote)
{
  unsigned long addr;
  uint8_t refs;
  int8_t  nid;

  int ret = -1;
  int hot_threshold = nr_walks + 1;
  int cold_threshold = 0;
  long promote_remain = 0;
  long demote_remain = 0;
  std::vector<void*> cold_page_array, hot_page_array;

  AddrSequence& page_refs
      = get_pagetype_refs(type).page_refs;
  const histogram_2d_type& refs_count
      = get_pagetype_refs(type).histogram_2d;

  if (nr_promote) {
    long save_nr_promote = nr_promote;

    for (hot_threshold = nr_walks; hot_threshold >= 1; --hot_threshold) {
      nr_promote -= refs_count[REF_LOC_PMEM][hot_threshold];
      if (nr_promote <= 0) {
        promote_remain = nr_promote + refs_count[REF_LOC_PMEM][hot_threshold];
        break;
      }
    }

    // cover no enough hot page case
    if (hot_threshold < 1) {
      hot_threshold = 1;
      promote_remain = refs_count[REF_LOC_PMEM][hot_threshold];
      fprintf(stderr, "WARNING: no enough HOT pages, request: %ld actual: %ld "
              "hot_threshold changed to: %d\n",
              save_nr_promote,
              save_nr_promote - nr_promote,
              hot_threshold);
    }
  }

  if (nr_demote) {
    long save_nr_demote = nr_demote;

    for (cold_threshold = 0; cold_threshold <= nr_walks; ++cold_threshold) {
      nr_demote -= refs_count[REF_LOC_DRAM][cold_threshold];
      if (nr_demote <= 0) {
        demote_remain = nr_demote + refs_count[REF_LOC_DRAM][cold_threshold];
        break;
      }
    }

    // cover no enough cold page case
    if (cold_threshold > nr_walks) {
      cold_threshold = nr_walks;
      demote_remain = refs_count[REF_LOC_DRAM][cold_threshold];
      fprintf(stderr, "WARNING: no enough COLD pages, request: %ld actual: %ld "
              "cold_threshold changed to: %d\n",
              save_nr_demote,
              save_nr_demote - nr_demote,
              cold_threshold);
    }
  }

  if (hot_threshold < cold_threshold + option.anti_thrash_threshold) {
    int save_hot_threshold = hot_threshold;

    hot_threshold = std::min(cold_threshold + option.anti_thrash_threshold,
                             nr_walks);
    promote_remain = refs_count[REF_LOC_PMEM][hot_threshold];
    fprintf(stderr, "NOTICE: anti-thrashing happend. "
            "cold_threshold: %d "
            "old hot_threshold: %d "
            "new hot_threshold: %d "
            "anti_thrash_threshold: %d\n",
            cold_threshold,
            save_hot_threshold,
            hot_threshold,
            option.anti_thrash_threshold);
  }

  // build the hot and cold page addr list
  ret = page_refs.get_first(addr, refs, nid);
  while(!ret) {

    if (!numa_collection->is_valid_nid(nid))
      goto NEXT;

    if (numa_collection->get_node(nid)->is_pmem()) {

      if ((refs == hot_threshold && promote_remain-- > 0)
          || refs > hot_threshold)
        hot_page_array.push_back((void*)addr);

    } else {

      if ((refs == cold_threshold && demote_remain-- > 0)
          || refs < cold_threshold)
        cold_page_array.push_back((void*)addr);

    }
NEXT:
    ret = page_refs.get_next(addr, refs, nid);
  }

  return ret;
}
