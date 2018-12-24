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

#include "lib/debug.h"
#include "lib/stats.h"
#include "GlobalScan.h"
#include "OptionParser.h"

extern OptionParser option;

const float GlobalScan::MIN_INTERVAL = 0.001;
const float GlobalScan::MAX_INTERVAL = 10;

#define HUGE_PAGE_SHIFT 21

GlobalScan::GlobalScan() : conf_reload_flag(0)
{
}

void GlobalScan::main_loop()
{
  unsigned max_round = option.nr_loops;
  if (!max_round)
    max_round = UINT_MAX;

  if (option.interval)
    interval = option.interval;
  else
    interval = option.initial_interval;

  create_threads();
  for (unsigned nround = 0; nround <= max_round; ++nround)
  {
    reload_conf();
    collect();
    walk_multi();
    count_refs();
    migrate();
    count_migrate_stats();
    if (exit_on_stabilized())
      break;
    if (exit_on_exceeded())
      break;

    double sleep_time = std::max(option.sleep_secs, 2 * interval);
    if (sleep_time > 10 * interval)
        sleep_time = 10 * interval;
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

  if (get_dram_anon_bytes() >= dram_free_anon_bytes)
      return true;

  return false;
}

int GlobalScan::collect()
{
  int err;

  idle_ranges.clear();

  if (option.get_policies().empty())
    err = process_collection.collect();
  else
    err = process_collection.collect(option.get_policies());

  if (option.debug_level >= 2)
    process_collection.dump();

  if (err)
    return err;

  for (auto &kv: process_collection.get_proccesses())
    for (auto &m: kv.second->get_ranges()) {
      m->set_throttler(&throttler);
      m->set_numacollection(&numa_collection);
      idle_ranges.push_back(m);
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


void GlobalScan::walk_multi()
{
  struct timeval ts1, ts2;
  float elapsed;

  nr_acceptable_scans = 0;

  for (auto& m: idle_ranges)
    m->prepare_walks(option.max_walks);

  printf("\nStarting page table scans:\n");
  printf("%7s  %8s  %23s  %23s  %15s\n", "nr_scan", "interval", "young", "top hot", "all");
  printf("====================================================================================\n");

  for (nr_walks = 0; nr_walks < option.max_walks;)
  {
    ++nr_walks;

    gettimeofday(&ts1, NULL);
    real_interval = tv_secs(last_scan_start, ts1);
    last_scan_start = ts1;

    walk_once();

    gettimeofday(&ts2, NULL);
    elapsed = tv_secs(ts1, ts2);

    if (should_stop_walk())
      break;

    update_interval(0);
    if (interval > elapsed)
      usleep((interval - elapsed) * 1000000);
  }

  printf("\n");
  update_interval(1);
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

unsigned long GlobalScan::get_dram_anon_bytes()
{
    unsigned long dram_anon = 0;
    unsigned long pages;

    if (option.hugetlb)
      sysfs.load_hugetlb();

    for(auto node: numa_collection.get_dram_nodes()) {
      int nid = node->id();
      if (option.hugetlb) {
        pages = sysfs.hugetlb(nid, "nr_hugepages") -
                sysfs.hugetlb(nid, "free_hugepages");
        dram_anon += pages << HUGE_PAGE_SHIFT;
      } else if (option.thp) {
        pages = proc_vmstat.vmstat(nid, "nr_anon_transparent_hugepages");
        dram_anon += pages << HUGE_PAGE_SHIFT;
      } else {
        pages = proc_vmstat.vmstat(nid, "nr_active_anon") +
                proc_vmstat.vmstat(nid, "nr_inactive_anon");
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

unsigned long GlobalScan::get_dram_free_anon_bytes()
{
    unsigned long dram_anon_capacity = 0;

    for (auto node: numa_collection.get_dram_nodes())
      dram_anon_capacity += proc_vmstat.anon_capacity(node->id());

    return dram_anon_capacity << PAGE_SHIFT;
}

void GlobalScan::update_dram_free_anon_bytes()
{
  proc_vmstat.clear();

  if (option.dram_percent) {
    dram_free_anon_bytes = option.dram_percent * all_bytes / 100;
  } else {
    dram_free_anon_bytes = get_dram_free_anon_bytes();
  }

  dram_hot_target = dram_free_anon_bytes / 2;
}

void GlobalScan::walk_once()
{
  int nr = 0;
  Job job;
  job.intent = JOB_WALK;

  young_bytes = 0;
  top_bytes = 0;
  all_bytes = 0;

  for (auto& m: idle_ranges)
  {
      job.migration = m;
      if (option.max_threads) {
        work_queue.push(job);
        printd("push job %d\n", nr);
        ++nr;
      } else {
        consumer_job(job);
        job.migration->gather_walk_stats(young_bytes, top_bytes, all_bytes);
      }
  }

  for (; nr; --nr)
  {
    printd("wait walk job %d\n", nr);
    job = done_queue.pop();
    job.migration->gather_walk_stats(young_bytes, top_bytes, all_bytes);
  }

  update_dram_free_anon_bytes();

  printf("%7d  %8.3f  %'15lu %6.2f%%  %'15lu %6.2f%%  %'15lu\n",
         nr_walks,
         (double)interval,
         young_bytes >> 10, 100.0 * young_bytes / all_bytes,
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

  proc_vmstat.show_numa_stats(&numa_collection);
  show_migrate_speed(tv_secs(ts_begin, ts_end));
}

void GlobalScan::show_migrate_speed(float delta_time)
{
  unsigned long migrated_kb = calc_migrated_bytes() >> 10;

  printf("Migration speed: moved %'lu KB in %.2f seconds (%'lu KB/sec)\n",
         migrated_kb, delta_time,
         (unsigned long)(migrated_kb / (delta_time + 0.0000001)));
}

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

  if (finished && nr_walks < option.max_walks / 4) {
    printd("interval %f x1.2 due to low nr_walks %d\n",
           (double) interval, nr_walks);
    interval *= 1.2;
  }
}

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
    total_moved_bytes += m->get_migrate_stats().get_moved_bytes();
  }

  return total_moved_bytes;
}
