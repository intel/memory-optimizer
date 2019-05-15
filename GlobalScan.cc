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

  if (!max_round || option.daemon)
    max_round = UINT_MAX;

  if (option.interval)
    interval = option.interval;
  else
    interval = option.initial_interval;

  create_threads();

  for (nround = 0; nround <= max_round; ++nround)
  {
    reload_conf();
    collect();
    update_pid_context();
    walk_multi();
    get_memory_type();
    count_refs();
    calc_migrate_parameter();
    migrate();
    count_migrate_stats();

    if (!option.daemon && exit_on_stabilized())
      break;

    if (!option.daemon && is_all_migration_done()) {
      printf("exit because all migration done.\n");
      break;
    }

    if (option.benchmark_mode && exit_on_benchmark()) {
      printf("Exit: benchmark mode migration done\n");
      break;
    }

    double sleep_time = std::max(option.sleep_secs, 2 * interval);

    if (is_all_migration_done()) {
      sleep_time = 20;
      printf("changed sleep_time to %f for all migration done.\n", sleep_time);
    }
    else if (sleep_time > 10 * interval)
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


void GlobalScan::walk_multi()
{
  struct timeval ts1, ts2;
  float elapsed;

  nr_acceptable_scans = 0;

  for (auto& m: idle_ranges)
    m->prepare_walks(option.max_walks);

  printf("\nStarting page table scans: %s\n", get_current_date().c_str());
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

  update_interval(1);
  printf("End of page table scans: %s\n", get_current_date().c_str());
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
          printf("pid = %d: mem = %ld kb, dram=%ld kb, pmem = %ld kb, dram_quota = %ld kb\n",
                 i.first,
                 mem_total_kb, mem_dram_kb, mem_pmem_kb,
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

void GlobalScan::calc_migrate_parameter()
{
  long total_mem ,total_pmem ,total_dram;
  long limit = option.one_period_migration_size;
  long dram_percent = option.dram_percent;
  long dram_target;
  long nr_demote, nr_promote;
  long delta;

  for (auto& m : idle_ranges) {
    for (const auto type : {PTE_ACCESSED, PMD_ACCESSED}) {
      long shift = pagetype_shift[type] - 10;

      total_pmem = m->get_total_memory_bytes(type, REF_LOC_PMEM) << shift;
      total_dram = m->get_total_memory_bytes(type, REF_LOC_DRAM) << shift;
      total_mem = total_pmem + total_dram;

      if (!total_mem) {
        nr_promote = 0;
        nr_demote = 0;
        goto set_nr;
      }

      if (dram_percent) {
        dram_target = total_mem * (dram_percent / 100.0);
        delta = dram_target - total_dram;
        delta = delta < 0 ?
                        std::max(delta, 0 - limit) :
                        std::min(delta, limit);
        nr_promote = (limit + delta) / 2;
        nr_demote = (limit - delta) / 2;
      } else  {
        nr_promote = limit / 2;
        nr_demote = limit / 2;
      }

set_nr:
      m->set_migrate_nr_promote(type, nr_promote >> shift);
      m->set_migrate_nr_demote(type, nr_demote >> shift);
    }
  }
}


bool GlobalScan::is_all_migration_done()
{
  for (auto &i : process_collection.get_proccesses()) {
    if (i.second->context.get_dram_quota() > 0)
      return false;
  }
  return true;
}

bool GlobalScan::exit_on_benchmark()
{
  for (auto &m : idle_ranges) {
    const MigrateResult& result = m->get_migrate_result();
    if (result.hot_threshold > result.cold_threshold + 1)
      return false;
  }
  return true;
}
