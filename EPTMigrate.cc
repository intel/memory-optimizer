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
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <linux/limits.h>

#include <numa.h>
#include <numaif.h>
#include "Option.h"
#include "EPTMigrate.h"
#include "lib/debug.h"
#include "lib/stats.h"
#include "AddrSequence.h"
#include "VMAInspect.h"
#include "Numa.h"
#include "BandwidthLimit.h"

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

void MigrateStats::show(Formatter& fmt, int mwhat)
{
  if (!to_move_kb)
    return;

  const char *type = (mwhat == HOT_MIGRATE ? "hot" : "cold");
  // const char *node = (mwhat == HOT_MIGRATE ? "DRAM" : "PMEM");

  fmt.print("\n");
  fmt.print("find %4s pages: %'14lu %3d%% of anon pages\n",
            type, to_move_kb, percent(to_move_kb, anon_kb));
  fmt.print("migrate failed: %'15lu %3d%% of %4s pages\n",
            skip_kb, percent(skip_kb, to_move_kb), type);
  fmt.print("migrate successful: %'11lu %3d%% of %4s pages\n",
            move_kb, percent(move_kb, to_move_kb), type);

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

  for (int i = COLD_MIGRATE; i < MAX_MIGRATE; ++i) {
    page_migrate_stats[i].clear();
    update_migrate_state(i);
  }

  for (auto& type : {PTE_ACCESSED, PMD_ACCESSED}) {
    if (!parameter[type].enabled) {
      printf("Skip %s migration: %s\n",
             pagetype_name[type],
             parameter[type].get_disable_reason());
      continue;
    }
    promote_and_demote(type);
  }

  for (int i = COLD_MIGRATE; i < MAX_MIGRATE; ++i)
    page_migrate_stats[i].show(fmt, i);

  if (policy.dump_distribution) {
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

int EPTMigrate::promote_and_demote(ProcIdlePageType type)
{
  unsigned long addr;
  uint8_t refs;
  int8_t  nid;
  int ret = -1;
  bool refs_in_range;

  long nr_promote = parameter[type].nr_promote;
  long nr_demote = parameter[type].nr_demote;

  std::vector<void*> addr_array_2d[MAX_MIGRATE];
  std::vector<int> target_nid_2d[MAX_MIGRATE];
  std::vector<int> from_nid_2d[MAX_MIGRATE];

  AddrSequence& page_refs
      = get_pagetype_refs(type).page_refs;

  if (nr_promote) {
    addr_array_2d[HOT_MIGRATE].reserve(nr_promote);
    from_nid_2d[HOT_MIGRATE].reserve(nr_promote);
    target_nid_2d[HOT_MIGRATE].reserve(nr_promote);
  }
  if (nr_demote) {
    addr_array_2d[COLD_MIGRATE].reserve(nr_demote);
    from_nid_2d[COLD_MIGRATE].reserve(nr_demote);
    target_nid_2d[COLD_MIGRATE].reserve(nr_demote);
  }

  ret = page_refs.get_first(addr, refs, nid);
  while(!ret) {

    if (!numa_collection->is_valid_nid(nid))
      goto next;

    if (numa_collection->get_node(nid)->is_pmem()) {
      refs_in_range = refs > parameter[type].hot_threshold
                      && refs <= parameter[type].hot_threshold_max;
      if ((refs == parameter[type].hot_threshold
          && parameter[type].promote_remain-- > 0)
          || refs_in_range)
        save_migrate_parameter((void*)addr, nid,
                               addr_array_2d[HOT_MIGRATE],
                               from_nid_2d[HOT_MIGRATE],
                               target_nid_2d[HOT_MIGRATE]);

    } else {
      refs_in_range = refs < parameter[type].cold_threshold
                      && refs >= parameter[type].cold_threshold_min;

      if ((refs == parameter[type].cold_threshold
          && parameter[type].demote_remain-- > 0)
          || refs_in_range)
        save_migrate_parameter((void*)addr, nid,
                               addr_array_2d[COLD_MIGRATE],
                               from_nid_2d[COLD_MIGRATE],
                               target_nid_2d[COLD_MIGRATE]);

    }
next:
    ret = page_refs.get_next(addr, refs, nid);
  }

  ret = do_interleave_move_pages(type,
                                 addr_array_2d,
                                 from_nid_2d, target_nid_2d);

  if (!option.progressive_profile.empty()) {
      call_progressive_profile_script(option.progressive_profile,
                                      parameter[type].cold_threshold,
                                      addr_array_2d[COLD_MIGRATE].size(),
                                      pagetype_size[type]);
      do_interleave_move_pages(type,
                               addr_array_2d,
                               target_nid_2d, from_nid_2d);
  }
  return ret;
}

int EPTMigrate::save_migrate_parameter(void* addr, int nid,
                                        std::vector<void*>& addr_array,
                                        std::vector<int>& from_nid_array,
                                        std::vector<int>& target_nid_array)
{
  NumaNode* peer_node;

  peer_node = numa_collection->get_node(nid)->get_peer_node();
  if (!peer_node) {
      fprintf(stderr, "WARNING: can NOT get target node id, skip addr. "
              "addr: 0x%p nid: %d\n",
              addr, nid);
      return -1;
  }

  addr_array.push_back(addr);
  from_nid_array.push_back(nid);
  target_nid_array.push_back(peer_node->id());
  return 0;
}

int EPTMigrate::do_interleave_move_pages(ProcIdlePageType type,
                                         std::vector<void*> *addr,
                                         std::vector<int> *from_nid,
                                         std::vector<int> *target_nid)
{
  size_t max_size;
  size_t batch_size;
  long count;
  long last_move_kb = 0;

  if (!addr[COLD_MIGRATE].size()
      && !addr[HOT_MIGRATE].size()) {
    fprintf(stderr,
            "NOTICE: skip migration: %s no HOT and COLD pages.\n",
            pagetype_name[type]);
    return 0;
  }

  // prepare the hot and cold migrator
  for (auto& i : page_migrator)
    setup_migrator(type, i);

  // get loop parameter
  max_size = std::max(addr[COLD_MIGRATE].size(),
                      addr[HOT_MIGRATE].size());
  batch_size = pagetype_batchsize[type];

  // interleave migration
  for (unsigned long i = 0; i < max_size; i += batch_size) {
    for (int migrate_type = 0; migrate_type < MAX_MIGRATE; ++migrate_type) {

      last_move_kb = page_migrate_stats[migrate_type].move_kb;
      if (i < addr[migrate_type].size()) {

        count = std::min(batch_size, addr[migrate_type].size() - i);
        page_migrator[migrate_type].move_pages(&addr[migrate_type][i],
                                               &target_nid[migrate_type][i],
                                               count);
        page_migrate_stats[migrate_type]
            .save_migrate_states(pagetype_shift[type],
                                 &from_nid[migrate_type][i],
                                 &target_nid[migrate_type][i],
                                 page_migrator[migrate_type].get_migration_result());
      }

      if (throttler)
        throttler->add_and_sleep((page_migrate_stats[migrate_type].move_kb - last_move_kb)
                                 * 1024);
    }
  }

  return 0;
}

void EPTMigrate::setup_migrator(ProcIdlePageType type, MovePages& migrator)
{
  migrator.set_pid(pid);
  migrator.set_page_shift(pagetype_shift[type]);
  migrator.set_batch_size(pagetype_batchsize[type]);
  migrator.set_migration_type(type);
  migrator.set_numacollection(numa_collection);
}

void EPTMigrate::update_migrate_state(int migrate_type)
{
  for (const ProcIdlePageType type: {PTE_ACCESSED, PMD_ACCESSED, PUD_PRESENT}) {
    AddrSequence& page_refs = get_pagetype_refs(type).page_refs;
    page_migrate_stats[migrate_type].anon_kb
        += page_refs.size() << (page_refs.get_pageshift() - 10);
  }
}

void EPTMigrate::call_progressive_profile_script(std::string& script_path_name,
                                                 int refs_count,
                                                 long page_count,
                                                 int page_size)
{
  pid_t child_pid;

  child_pid = fork();
  if (child_pid == -1) {
    return;
  }

  if (child_pid > 0) {
    waitpid(child_pid, NULL, 0);
  } else if (0 == child_pid) {
    const char* cmdline = script_path_name.c_str();
    char buf[128];
    struct {
      const char* name;
      const char* format_str;
      long value;
    } env_list[] = {
      { "REF_COUNT",  "%ld", refs_count },
      { "PAGE_SIZE",  "%ld", page_size  },
      { "PAGE_COUNT", "%ld", page_count },
      { "TARGET_PIDS","%ld", pid        },
    };

    for (size_t i = 0; i < sizeof(env_list)/sizeof(env_list[0]); ++i) {
      snprintf(buf, sizeof(buf),
               env_list[i].format_str,
               env_list[i].value);
      setenv(env_list[i].name, buf, true);
    }

    if (execl(cmdline, cmdline, (char*)NULL) == -1) {
      perror("failed to execl(): ");
      exit(-1);
    }
  }
}
