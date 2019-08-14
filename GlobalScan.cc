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

#include <thread>
#include <atomic>
#include <iostream>
#include <unistd.h>
#include <limits.h>
#include <sys/time.h>
#include <vector>

#include "lib/debug.h"
#include "lib/stats.h"
#include "GlobalScan.h"
#include "OptionParser.h"
#include "VMAInspect.h"

using namespace std;
extern OptionParser option;

const float GlobalScan::MIN_INTERVAL = 0.001;
const float GlobalScan::MAX_INTERVAL = 10;

#define HUGE_PAGE_SHIFT 21

static string get_current_date()
{
  time_t now = time(0);
  char tmp[64] = {"unknown time"};
  struct tm* loc_time;

  loc_time = localtime(&now);
  if (loc_time)
    strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", loc_time);

  return tmp;
}

GlobalScan::GlobalScan() : conf_reload_flag(0)
{
}

void GlobalScan::main_loop()
{
  unsigned max_round = option.nr_loops;
  int nr_scan_rounds = 0;
  struct timeval ts_begin;
  struct timeval ts_end;
  float sleep_time;
  float elapsed;
  float walk_interval;
  float idle_sleep_time = 20;

  if (!max_round)
    max_round = UINT_MAX;

  if (option.interval)
    interval = option.interval;
  else
    interval = option.initial_interval;

  create_threads();

  for (nround = 0; nround <= max_round; ++nround) {
    gettimeofday(&ts_begin, NULL);

    if (0 == nr_scan_rounds) {
      reload_conf();
      collect();

      if (idle_ranges.empty()) {
        printf("No target process, sleeping for %f seconds\n", idle_sleep_time);
        usleep(idle_sleep_time * 1000000);
        continue;
      }

      prepare_walk_multi();
    }

    walk_interval = walk_multi();

    if (++nr_scan_rounds < option.nr_scan_rounds)
      goto have_sleep;

    nr_scan_rounds = 0;
    count_refs();
    calc_memory_size();

    if (option.progressive_profile.empty()) {
      calc_migrate_parameter();
      migrate();
      count_migrate_stats();
      calc_hotness_drifting();
      save_idle_ranges_last();
    } else {
      progressive_profile();
      break;
    }

    if (option.exit_on_converged && exit_on_converged()) {
      printf("Exit: exit_on_converged done\n");
      break;
    }

have_sleep:
    gettimeofday(&ts_end, NULL);
    elapsed = tv_secs(ts_begin, ts_end);
    sleep_time = std::max(2 * walk_interval,
                          option.scan_period - elapsed);

    printf("\nSleeping for %.2f seconds\n", sleep_time);

    usleep(sleep_time * 1000000);
  }
  stop_threads();
}

// auto exit for stable benchmarks
bool GlobalScan::exit_on_stabilized()
{
    if (!option.exit_on_stabilized)
      return false;

    if (EPTMigrate::sys_migrate_stats.move_kb    * 100 >
        EPTMigrate::sys_migrate_stats.to_move_kb * option.exit_on_stabilized)
      return false;

    printf("exit_on_stabilized: move=%'luM << to_move=%'luM\n",
           EPTMigrate::sys_migrate_stats.move_kb    >> 10,
           EPTMigrate::sys_migrate_stats.to_move_kb >> 10);
    return true;
}

// use scheme:
//   migration: hot
//   dram_percent: xx
//   initial placement: all in PMEM nodes
//   final placement: dram_percent hot pages moved to DRAM nodes
bool GlobalScan::exit_on_exceeded()
{
  if (!option.exit_on_exceeded)
    return false;

  unsigned long bytes = get_dram_anon_bytes(false);
  if (bytes < dram_free_anon_bytes)
    return false;

  printf("exit_on_exceeded: %'luK : %'luK = %d%%\n",
         bytes >> 10, dram_free_anon_bytes >> 10,
         percent(bytes, dram_free_anon_bytes));
  return true;
}

int GlobalScan::collect()
{
  int err;

  idle_ranges.clear();

  if (option.get_policies().empty())
    err = process_collection.collect();
  else
    err = process_collection.collect(option.get_policies());

  if (option.dump_processes || option.debug_level >= 2)
    process_collection.dump();

  if (err)
    return err;

  for (auto &kv: process_collection.get_proccesses()) {
    for (auto &m: kv.second->get_ranges()) {
      m->set_throttler(&throttler);
      m->set_numacollection(&numa_collection);
      idle_ranges.push_back(m);
    }
  }

  return 0;
}

void GlobalScan::create_threads()
{
  worker_threads.reserve(option.max_threads);

  for (int i = 0; i < option.max_threads; ++i)
    worker_threads.push_back(std::thread(&GlobalScan::consumer_loop, this));
}

void GlobalScan::stop_threads()
{
  Job job;
  job.intent = JOB_QUIT;

  for (unsigned long i = 0; i < worker_threads.size(); ++i)
    work_queue.push(job);

  for (auto& th : worker_threads)
    th.join();
}

void GlobalScan::prepare_walk_multi()
{
  nr_walks = 0;
  for (auto& m: idle_ranges)
    m->prepare_walks(option.nr_scans);
}

float GlobalScan::walk_multi()
{
  struct timeval ts1, ts2;
  float elapsed;
  float interval_sum = 0;
  int scans;
  float sleep_time;
  std::vector<float> sleep_time_vector;
  nr_acceptable_scans = 0;

  printf("\nStarting page table scans: %s\n", get_current_date().c_str());
  printf("Auto-interval: %s\n",
         should_target_aep_young() ? "aep_young" : "young");
  printf("%7s  %8s  %23s  %23s  %23s  %15s\n",
         "nr_scan", "interval", "young", "aep_young", "top hot", "all");
  printf("================================================================="
         "============================================\n");

  sleep_time_vector.reserve(option.nr_scans);
  for (scans = 0; scans < option.nr_scans;) {
    ++scans;

    gettimeofday(&ts1, NULL);
    if (nr_total_scans > 0)
      real_interval = tv_secs(last_scan_start, ts1);
    else
      real_interval = 0.0;

    last_scan_start = ts1;

    walk_once(scans);

    gettimeofday(&ts2, NULL);
    elapsed = tv_secs(ts1, ts2);

    interval_sum += interval;

    update_interval();

    sleep_time = interval - elapsed;
    sleep_time_vector.push_back(sleep_time);
    if (sleep_time > 0) {
      usleep(sleep_time * 1000000);
    }

    // for handling overflow case
    if (!(++nr_total_scans))
      nr_total_scans = 1;
  }

  for (size_t i = 0; i < sleep_time_vector.size(); ++i)
    printf("sleep time %2lu -> %-2lu: %f\n", i+1, i+2, sleep_time_vector[i]);

  // must update nr_walks to align with idle_ranges::nr_walks.
  nr_walks += scans;

  printf("End of page table scans: %s\n", get_current_date().c_str());

  return interval_sum / scans;
}

void GlobalScan::count_refs()
{
  EPTScan::reset_sys_refs_count(nr_walks);

  for (auto& m: idle_ranges)
    m->count_refs();

  EPTScan::save_counts(option.output_file);
}

void GlobalScan::count_migrate_stats()
{
  EPTMigrate::reset_sys_migrate_stats();

  for (auto& m: idle_ranges)
    m->count_migrate_stats();
}

unsigned long GlobalScan::get_dram_anon_bytes(bool is_include_free_page)
{
    unsigned long dram_anon = 0;
    unsigned long pages;
    unsigned long pages_4k = 0;

    if (option.hugetlb)
      sysfs.load_hugetlb();

    for(auto node: numa_collection.get_dram_nodes()) {
      int nid = node->id();
      if (option.hugetlb) {
        pages = sysfs.hugetlb(nid, "nr_hugepages");
        if (!is_include_free_page)
          pages -= sysfs.hugetlb(nid, "free_hugepages");

        dram_anon += pages << HUGE_PAGE_SHIFT;
      } else if (option.thp) {
        pages = proc_vmstat.vmstat(nid, "nr_anon_transparent_hugepages");
        if (is_include_free_page)
          pages_4k = proc_vmstat.vmstat(nid, "nr_free_pages");

        dram_anon += pages << HUGE_PAGE_SHIFT;
        dram_anon += pages_4k << PAGE_SHIFT;
      } else {
        pages = proc_vmstat.vmstat(nid, "nr_active_anon") +
                proc_vmstat.vmstat(nid, "nr_inactive_anon");
        if (is_include_free_page)
          pages += proc_vmstat.vmstat(nid, "nr_free_pages");

        dram_anon += pages << PAGE_SHIFT;
      }
    }

    return dram_anon;
}

// similar to EPTScan::should_stop()
bool GlobalScan::should_stop_walk()
{
  // page_refs.get_top_bytes() is 0 when nr_walks == 1
  if (nr_walks <= 2)
    return false;

  if (top_bytes < target_hot_bytes())
    return true;

  if (top_bytes < accept_hot_bytes()) {
    if (++nr_acceptable_scans >= 3)
      return true;
  } else
    nr_acceptable_scans = 0;

  return false;
}

void GlobalScan::update_dram_free_anon_bytes()
{
  proc_vmstat.clear();

  if (option.dram_percent) {
    dram_free_anon_bytes = option.dram_percent * all_bytes / 100;
  } else {
    dram_free_anon_bytes = get_dram_free_and_anon_bytes();
  }

  dram_hot_target = dram_free_anon_bytes / 2;

  if (option.exit_on_exceeded)
    // make sure to exit within 16 rounds of scan
    dram_hot_target += dram_hot_target * nround / 16;
}

void GlobalScan::walk_once(int scans)
{
  int nr = 0;
  Job job;
  job.intent = JOB_WALK;

  young_bytes = 0;
  top_bytes = 0;
  pmem_young_bytes = 0;
  all_bytes = 0;

  for (auto& m: idle_ranges) {

    job.migration = m;
    if (option.max_threads) {
      work_queue.push(job);
      printd("push job %d\n", nr);
    } else {
      consumer_job(job);
      done_queue.push(job);
    }
    ++nr;
  }

  for (; nr; --nr) {
    printd("wait walk job %d\n", nr);
    job = done_queue.pop();

    if (1 == scans)
      job.migration->get_memory_type();
    job.migration->gather_walk_stats(young_bytes,
                                     pmem_young_bytes,
                                     top_bytes, all_bytes);
  }

  update_dram_free_anon_bytes();

  printf("%7d  %8.3f  %'15lu %6.2f%%  %'15lu %6.2f%%  %'15lu %6.2f%%  %'15lu\n",
         scans,
         (double)real_interval,
         young_bytes >> 10, 100.0 * young_bytes / all_bytes,
         pmem_young_bytes >> 10, 100.0 * pmem_young_bytes / all_bytes,
         top_bytes >> 10, 100.0 * top_bytes / all_bytes,
         all_bytes >> 10);
}

int GlobalScan::consumer_job(Job& job)
{
    switch(job.intent)
    {
    case JOB_WALK:
      job.migration->walk();
      break;
    case JOB_MIGRATE:
      job.migration->migrate();
      break;
    case JOB_QUIT:
      printd("consumer_loop quit job\n");
      return 1;
    }

    return 0;
}

void GlobalScan::consumer_loop()
{
  printd("consumer_loop started\n");
  for (;;)
  {
    Job job = work_queue.pop();
    int ret = consumer_job(job);
    if (ret)
      break;
    printd("consumer_loop done job\n");
    done_queue.push(job);
  }
}

void GlobalScan::migrate()
{
  timeval ts_begin, ts_end;
  int nr = 0;
  Job job;

  job.intent = JOB_MIGRATE;

  printf("\nStarting migration: %s\n", get_current_date().c_str());
  gettimeofday(&ts_begin, NULL);
  for (auto& m: idle_ranges)
  {
      job.migration = m;
      if (option.max_threads) {
        work_queue.push(job);
        ++nr;
      } else
        consumer_job(job);
  }

  for (; nr; --nr)
  {
    printd("wait migrate job %d\n", nr);
    job = done_queue.pop();
  }
  gettimeofday(&ts_end, NULL);
  printf("\nEnd of migration: %s\n", get_current_date().c_str());

  if (option.show_numa_stats)
    proc_vmstat.show_numa_stats(&numa_collection);

  show_migrate_speed(tv_secs(ts_begin, ts_end));
}

void GlobalScan::progressive_profile()
{
  Job job;
  int nr = 0;

  job.intent = JOB_MIGRATE;

  for (int i = 0; i < nr_walks; ++i) {
    calc_progressive_profile_parameter(REF_LOC_DRAM, i);

    printf("\nStarting progressive_profile migration for refs %d : %s\n",
           i, get_current_date().c_str());
    for (auto& m: idle_ranges)
    {
      job.migration = m;
      if (option.max_threads) {
        work_queue.push(job);
        ++nr;
      } else
        consumer_job(job);
    }

    for (; nr; --nr)
    {
      printd("wait migrate job %d\n", nr);
      job = done_queue.pop();
    }
    printf("\nEnd of migration: %s\n", get_current_date().c_str());
  }
}

void GlobalScan::show_migrate_speed(float delta_time)
{
  unsigned long migrated_kb = calc_migrated_bytes() >> 10;

  printf("Migration speed: moved %'lu KB in %.2f seconds (%'lu KB/sec)\n",
         migrated_kb, delta_time,
         (unsigned long)(migrated_kb / (delta_time + 0.0000001)));
}

#if 0
void GlobalScan::update_interval(bool finished)
{
  if (option.interval)
    return;

  if (nr_walks <= 1)
    return;

  float ratio = target_young_bytes() / (young_bytes + 1.0);
  if (ratio > 10)
    ratio = 10;
  else if (ratio < 0.2)
    ratio = 0.2;

  printd("interval %f real %f * %.1f for young %.2f%%\n",
         (double) interval,
         (double) real_interval,
         (double) ratio,
         (double) 100 * young_bytes / (all_bytes + 1));

  interval = real_interval * ratio;
  if (interval < 0.000001)
    interval = 0.000001;
  if (interval > 100)
    interval = 100;

  if (finished && nr_walks < option.max_walks / 4) {
    printd("interval %f x1.2 due to low nr_walks %d\n",
           (double) interval, nr_walks);
    interval *= 1.2;
  }

  if (finished) {
    printf("target_young: %'luKB  %d%%  %d%%\n", target_young_bytes() >> 10,
                                         percent(target_young_bytes(), young_bytes),
                                         percent(target_young_bytes(), all_bytes));
    printf("target_hot:   %'luKB  %d%%  %d%%\n", target_hot_bytes() >> 10,
                                         percent(target_hot_bytes(), top_bytes),
                                         percent(target_hot_bytes(), all_bytes));
    printf("\n");
  }
}

#else

void GlobalScan::update_interval()
{
  unsigned long target_bytes;
  unsigned long young;
  if (option.interval)
    return;

  if (0 == nr_total_scans)
    return;

  if (should_target_aep_young()) {
    target_bytes = option.one_period_migration_size * 1024UL
                   * option.interval_scale / 100;
    young = pmem_young_bytes;
  } else {
    target_bytes = all_bytes * option.dram_percent / 100;
    young = young_bytes;
  }

  intervaler.set_target_y(target_bytes);
  intervaler.add_pair(real_interval, young);
  interval = intervaler.estimate_x();
  if (interval > 10)
    interval = 10;
}

#endif
void GlobalScan::request_reload_conf()
{
  conf_reload_flag.store(1, std::memory_order_relaxed);
}

void GlobalScan::reload_conf()
{
  int flag = conf_reload_flag.exchange(0, std::memory_order_relaxed);
  if (flag) {
    printf("start to reload conf file.\n");
    option.reparse();
    apply_option();
  }
}

void GlobalScan::apply_option()
{
  throttler.set_bwlimit_mbps(option.bandwidth_mbps);
  numa_collection.collect(&option.numa_hw_config,
                          &option.numa_hw_config_v2);
}

unsigned long GlobalScan::calc_migrated_bytes()
{
  unsigned long total_moved_bytes = 0;

  for (auto& m : idle_ranges) {
    for (unsigned i = COLD_MIGRATE; i < MAX_MIGRATE; ++i)
      total_moved_bytes += m->get_migrate_stats(i).get_moved_bytes();
  }

  return total_moved_bytes;
}

// TODO: below function be deprecated soon once 1:1 mode is done.
void GlobalScan::update_pid_context()
{
  int err;
  VMAInspect inspector;
  unsigned long mem_total_kb;
  unsigned long mem_dram_kb;
  unsigned long mem_pmem_kb;
  double ratio = option.dram_percent / 100.0;

  inspector.set_numa_collection(&numa_collection);
  for(auto &i : process_collection.get_proccesses()) {
      err = inspector.calc_memory_state(i.first,
                                        mem_total_kb,
                                        mem_dram_kb,
                                        mem_pmem_kb);
      if (err) {
          fprintf(stderr, "calc_memory_state failed in %s: err = %d",
                  __func__, err);
          continue;
      }

      // update dram quota
      {
          long dram_quota = mem_total_kb * ratio - mem_dram_kb;
          i.second->context.set_dram_quota(dram_quota);
          printf("pid: %d mem: %ld kb dram: %ld kb pmem: %ld kb ratio: %d dram_quota: %ld kb\n",
                 i.first,
                 mem_total_kb, mem_dram_kb, mem_pmem_kb,
                 percent(mem_dram_kb, mem_total_kb),
                 i.second->context.get_dram_quota());
      }
  }
}

void GlobalScan::get_memory_type()
{
  for (auto& m : idle_ranges) {
    m->get_memory_type();
  }
}

void GlobalScan::calc_memory_size()
{
  global_total_pmem = 0;
  global_total_dram = 0;
  global_total_mem = 0;
  for (const auto type : {PTE_ACCESSED, PMD_ACCESSED}) {
    long shift = pagetype_shift[type] - 10;

    total_pmem[type] = EPTScan::get_total_memory_page_count(type, REF_LOC_PMEM) << shift;
    total_dram[type] = EPTScan::get_total_memory_page_count(type, REF_LOC_DRAM) << shift;
    total_mem[type] = total_pmem[type] + total_dram[type];

    global_total_pmem += total_pmem[type];
    global_total_dram += total_dram[type];
  }
  global_total_mem = global_total_pmem + global_total_dram;
  global_dram_ratio = (100.0 * global_total_dram) / global_total_mem;

  printf("global memory size state: total: %ld KB dram: %ld KB pmem: %ld KB\n"
         "ratio: %ld target ratio: %d\n",
         global_total_mem,
         global_total_dram, global_total_pmem,
         global_dram_ratio, option.dram_percent);
}

void GlobalScan::calc_hotness_drifting()
{
  bool found;
  size_t i, j;
  int ret;

  if (idle_ranges_last.empty())
    return;

  if (!option.split_rss_size.empty()) {
    printf("WARNING: hotness drifting calculation can NOT work with split_rss_size option,\n"
           "please remove the option and try again.\n");
    return;
  }

  for (i = 0; i < idle_ranges_last.size(); ++i) {
    pid_t pid_last = idle_ranges_last[i]->get_pid();

    for (found = false, j = 0; j < idle_ranges.size(); ++j) {
      if (idle_ranges[j]->get_pid() == pid_last) {
        found = true;
        break;
      }
    }

    if (!found) {
      printf("WARNING: failed to find pid: Skip hotness drifting calculation for pid %d\n", pid_last);
      continue;
    }

    ret = 0;
    ret += idle_ranges_last[i]->normalize_page_hotness();
    ret += idle_ranges[j]->normalize_page_hotness();
    if (ret) {
      printf("WARNING: failed to normalize page hotness: Skip hotness drifting calculation for pid %d\n",
             pid_last);
      continue;
    }

    calc_page_hotness_drifting(idle_ranges_last[i], idle_ranges[j]);
  }

  return;
}

void GlobalScan::calc_page_hotness_drifting(EPTMigratePtr last,
                                            EPTMigratePtr current)
{
  const int j_end = 2;
  int8_t unused_nid;

  long stable_hotness_count[MAX_ACCESSED];
  long unstable_hotness_count[MAX_ACCESSED];
  long total_count[MAX_ACCESSED];
  int8_t final_hotness;

  int rc[j_end];
  unsigned long addr[j_end];
  uint8_t hotness[j_end];
  AddrSequence* addr_seq[j_end];

  for (int i = 0; i < MAX_ACCESSED; ++i) {
    addr_seq[0] = &last->get_pagetype_refs((ProcIdlePageType)i).page_refs;
    addr_seq[1] = &current->get_pagetype_refs((ProcIdlePageType)i).page_refs;

    stable_hotness_count[i] = 0;
    unstable_hotness_count[i] = 0;
    total_count[i] = 0;

    if (addr_seq[0]->empty() && addr_seq[1]->empty())
      continue;

    for (int j = 0; j < j_end; ++j)
      rc[j] = addr_seq[j]->get_first(addr[j], hotness[j], unused_nid);

    while(!rc[0] && !rc[1]) {
      if (addr[0] < addr[1]) {
        if (hotness[0] == 1)
          ++unstable_hotness_count[i];

        rc[0] = addr_seq[0]->get_next(addr[0], hotness[0], unused_nid);
        continue;
      }

      if (addr[0] > addr[1]) {
        if (hotness[1] == 1)
          ++unstable_hotness_count[i];

        rc[1] = addr_seq[1]->get_next(addr[1], hotness[1], unused_nid);
        continue;
      }

      final_hotness = hotness[0] + hotness[1];
      if (final_hotness == 1)
        ++unstable_hotness_count[i];
      else if (final_hotness == 2)
        ++stable_hotness_count[i];

      for (int j = 0; j < j_end; ++j)
        rc[j] = addr_seq[j]->get_next(addr[j], hotness[j], unused_nid);
    }
  }

  printf("\nPage hotness drifting for PID %d:\n", last->get_pid());
  for (int i = 0; i < MAX_ACCESSED; ++i) {
    unstable_hotness_count[i] /= 2;
    total_count[i] = stable_hotness_count[i] + unstable_hotness_count[i];
    printf("%-12s: stable pages:%-16ld unstable pages:%-16ld drifting percent:%d%%\n",
           pagetype_name[i],
           stable_hotness_count[i],
           unstable_hotness_count[i],
           percent(unstable_hotness_count[i], total_count[i]));
  }
  printf("\n");
}

bool GlobalScan::in_adjust_ratio_stage()
{
  long error;

  if (!option.dram_percent)
    return false;

  error = global_dram_ratio - option.dram_percent;
  return error ? true : false;
}

bool GlobalScan::should_target_aep_young()
{
  if (!option.progressive_profile.empty())
    return false;

  if (global_dram_ratio <= 0 || global_dram_ratio >= 90)
    return false;

  return true;
}

 void GlobalScan::calc_progressive_profile_parameter(ref_location from_type, int page_refs)
{
  long move_count;

  for (const auto type : {PTE_ACCESSED, PMD_ACCESSED}) {

    if (!total_mem[type]) {
      for (auto& range : idle_ranges) {
        init_migration_parameter(range, type);
        range->parameter[type].disable("No HOT or COLD pages");
      }
      continue;
    }

    printf("\nMemory info by %s for %s:\n"
           "  total_mem: %ld kb\n"
           "  total_dram: %ld kb\n"
           "  total_pmem: %ld kb\n"
           "  ratio: %d\n",
           __func__, pagetype_name[type],
           total_mem[type], total_dram[type], total_pmem[type],
           percent(total_dram[type], total_mem[type]));

    for (auto& range : idle_ranges) {
      const histogram_2d_type& refs_count
          = range->get_pagetype_refs(type).histogram_2d;

      init_migration_parameter(range, type);
      move_count = refs_count[from_type][page_refs];

      if (REF_LOC_DRAM == from_type) {
        range->parameter[type].nr_demote = move_count;
        range->parameter[type].demote_remain = move_count;
        range->parameter[type].cold_threshold = page_refs;
        range->parameter[type].cold_threshold_min = page_refs;
        range->parameter[type].enable();
      } else if (REF_LOC_PMEM == from_type) {
        range->parameter[type].nr_promote = move_count;
        range->parameter[type].promote_remain = move_count;
        range->parameter[type].hot_threshold = page_refs;
        range->parameter[type].hot_threshold_max = page_refs;
        range->parameter[type].enable();
      } else  {
        range->parameter[type].disable("wrong from_type");
      }
    }

    printf("\nPage selection for %s:\n", pagetype_name[type]);
    for (auto& range : idle_ranges) {
      const migrate_parameter& parameter = range->parameter[type];
      range->dump_histogram(type);
      printf("migration parameter dump:\n");
      parameter.dump();
      printf("\n");
    }
  }
}

void GlobalScan::calc_migrate_parameter()
{
  long limit = option.one_period_migration_size;
  long dram_percent = option.dram_percent;
  long dram_target;
  long nr_demote, nr_promote;
  long save_nr_promote, save_nr_demote;
  long nr_increase;
  long delta;
  int hot_threshold = nr_walks + 1;
  int cold_threshold = -1;

  for (const auto type : {PTE_ACCESSED, PMD_ACCESSED}) {
    long shift = pagetype_shift[type] - 10;

    if (!total_mem[type]) {
      for (auto& range : idle_ranges) {
        init_migration_parameter(range, type);
        range->parameter[type].disable("No HOT or COLD pages");
      }
      continue;
    }

    if (dram_percent) {
      long adjust = limit / 8;
      dram_target = total_mem[type] * (dram_percent / 100.0);
      delta = dram_target - total_dram[type];
      delta = delta < 0 ?
                      std::max(delta, 0 - limit + adjust) :
                      std::min(delta, limit - adjust);
      nr_promote = (limit + delta) / 2;
      nr_demote = (limit - delta) / 2;
    } else  {
      nr_promote = limit / 2;
      nr_demote = limit / 2;
    }

    // nr_promote = std::min(total_pmem[type], nr_promote);
    // nr_demote = std::min(total_dram[type], nr_demote);

    printf("\nMemory info by %s for %s:\n"
           "  total_mem: %ld kb\n"
           "  total_dram: %ld kb\n"
           "  total_pmem: %ld kb\n"
           "  ratio: %d\n"
           "  promote: %ld kb\n"
           "  demote: %ld kb\n",
           __func__, pagetype_name[type],
           total_mem[type], total_dram[type], total_pmem[type],
           percent(total_dram[type], total_mem[type]),
           nr_promote, nr_demote);

    // from KB to page count
    nr_promote >>= shift;
    nr_demote  >>= shift;
    save_nr_promote = nr_promote;
    save_nr_demote = nr_demote;
    for (int i = 0; i <= nr_walks; ++i) {
      for (auto& range : idle_ranges) {
        const histogram_2d_type& refs_count
            = range->get_pagetype_refs(type).histogram_2d;

        // Skip disabled range (for example fail to anti-thrashing)
        if (0 == i)
          init_migration_parameter(range, type);
        else if (!range->parameter[type].enabled)
          continue;

        if (nr_promote > 0) {
          hot_threshold = nr_walks - i;
          nr_increase = std::min(nr_promote,
                                 (long)refs_count[REF_LOC_PMEM][hot_threshold]);

          range->parameter[type].nr_promote += nr_increase;
          range->parameter[type].promote_remain = nr_increase;
          range->parameter[type].hot_threshold = hot_threshold;
          range->parameter[type].hot_threshold_max = nr_walks;
          range->parameter[type].enable();

          nr_promote -= nr_increase;
        }

        if (nr_demote > 0) {
          cold_threshold = i;
          nr_increase = std::min(nr_demote,
                                 (long)refs_count[REF_LOC_DRAM][cold_threshold]);

          range->parameter[type].nr_demote += nr_increase;
          range->parameter[type].demote_remain = nr_increase;
          range->parameter[type].cold_threshold = cold_threshold;
          range->parameter[type].cold_threshold_min = 0;
          range->parameter[type].enable();

          nr_demote -= nr_increase;
        }

        if (!in_adjust_ratio_stage())
          anti_thrashing(range, type, option.anti_thrash_threshold);
      }
    }

    // no enough hot pages case
    if (nr_promote > 0) {
      fprintf(stderr, "WARNING: No enough %s HOT pages:\n"
              "request: %ld actual: %ld\n"
              "hot_threshold changed to: %d\n",
              pagetype_name[type],
              save_nr_promote,
              save_nr_promote - nr_promote,
              hot_threshold);
    }
    // no enough cold pages case
    if (nr_demote > 0) {
      fprintf(stderr, "WARNING: No enough %s COLD pages:\n"
              "request: %ld actual: %ld\n"
              "cold_threshold changed to: %d\n",
              pagetype_name[type],
              save_nr_demote,
              save_nr_demote - nr_demote,
              cold_threshold);
    }

    printf("\nPage selection for %s:\n", pagetype_name[type]);
    for (auto& range : idle_ranges) {
      const migrate_parameter& parameter = range->parameter[type];
      range->dump_histogram(type);
      printf("global nr_promte: %ld global nr_demote: %ld\n",
             save_nr_promote, save_nr_demote);
      printf("migration parameter dump:\n");
      parameter.dump();
      printf("\n");
    }
  }
}

void GlobalScan::anti_thrashing(EPTMigratePtr range, ProcIdlePageType type,
                                int anti_threshold)
{
  int hot_threshold;
  int cold_threshold;

  const histogram_2d_type& refs_count
        = range->get_pagetype_refs(type).histogram_2d;

  struct migrate_parameter& parameter
        = range->parameter[type];

  hot_threshold = parameter.hot_threshold;
  cold_threshold = parameter.cold_threshold;

  // anti-thrashing only need for "bi-direction" migration
  if (cold_threshold < 0 || hot_threshold > nr_walks)
    return;

  if (hot_threshold - cold_threshold >= option.anti_thrash_threshold)
    return;


  if (hot_threshold == nr_walks) {
    parameter.cold_threshold = std::max(hot_threshold - anti_threshold, 0);
    parameter.demote_remain = std::min((long)refs_count[REF_LOC_DRAM][parameter.cold_threshold],
                                       parameter.nr_demote);
  } else {
    parameter.hot_threshold = std::min(cold_threshold + anti_threshold, nr_walks);
    parameter.promote_remain = std::min((long)refs_count[REF_LOC_PMEM][parameter.hot_threshold],
                                        parameter.nr_promote);
  }

  fprintf(stderr, "NOTICE: %s anti-thrashing happend:\n"
          "anti_thrash_threshold: %d\n"
          "cold_threshold: %d -> %d\n"
          "hot_threshold:  %d -> %d\n",
          pagetype_name[type],
          option.anti_thrash_threshold,
          cold_threshold, parameter.cold_threshold,
          hot_threshold, parameter.hot_threshold);

  if (hot_threshold - cold_threshold < option.anti_thrash_threshold) {
    fprintf(stderr,
            "NOTICE: skip migration: %s hot_threshold - cold_threshold < %d.\n",
            pagetype_name[type],
            option.anti_thrash_threshold);
    parameter.disable("fail to anti-thrashing");
  }
}

void GlobalScan::init_migration_parameter(EPTMigratePtr range, ProcIdlePageType type)
{
  range->parameter[type].clear();
  range->parameter[type].hot_threshold = nr_walks + 1;
  range->parameter[type].hot_threshold_max = nr_walks + 1;
  range->parameter[type].cold_threshold = -1;
  range->parameter[type].cold_threshold_min = -1;
  range->parameter[type].disable("by page selection");
}

bool GlobalScan::is_all_migration_done()
{
  for (auto &i : process_collection.get_proccesses()) {
    if (i.second->context.get_dram_quota() > 0)
      return false;
  }
  return true;
}

bool GlobalScan::exit_on_converged()
{
  ProcIdlePageType page_type[] = {PTE_ACCESSED, PMD_ACCESSED};
  bool converged;
  int  converged_count = 0;
  int  nr_check_count = sizeof(page_type)/sizeof(page_type[0])
                        * (int)idle_ranges.size();
  int  valid_count;

  for (auto &m : idle_ranges) {
    for (auto &type : page_type) {
      const migrate_parameter& parameter = m->parameter[type];

      valid_count = 0;
      if (parameter.hot_threshold <= nr_walks)
        ++valid_count;
      if (parameter.cold_threshold >= 0)
        ++valid_count;

      if (valid_count > 1)
        converged = parameter.enabled ?
                 (parameter.hot_threshold - parameter.cold_threshold <=
                  option.anti_thrash_threshold) : true;
      else
        converged = true;

      printf("exit_on_converged: %2s hot_threshold: %2d cold_threshold: %2d converged: %d\n",
             pagetype_name[type],
             parameter.hot_threshold, parameter.cold_threshold,
             (int)converged);
      converged_count += (int)converged;
    }
  }

  if (in_adjust_ratio_stage()) {
    printf("exit_on_converged: in ratio adjustment stage: %ld%%\n", global_dram_ratio);
    return false;
  }

  if (converged_count < nr_check_count)
    return false;

  return true;
}
