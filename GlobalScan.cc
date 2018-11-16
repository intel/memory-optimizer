
#include <thread>
#include <atomic>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>

#include "lib/debug.h"
#include "GlobalScan.h"
#include "OptionParser.h"

extern OptionParser option;

const float GlobalScan::MIN_INTERVAL = 0.001;
const float GlobalScan::MAX_INTERVAL = 10;
const float GlobalScan::INITIAL_INTERVAL = 0.1;

GlobalScan::GlobalScan() : conf_reload_flag(0)
{
}

void GlobalScan::main_loop()
{
  int nloop = option.nr_loops;

  if (option.interval)
    interval = option.interval;
  else
    interval = INITIAL_INTERVAL;

  create_threads();
  for (; !option.nr_loops || nloop-- > 0;)
  {
    reload_conf();
    collect();
    walk_multi();
    count_refs();
    migrate();

    double sleep_time = std::max(option.sleep_secs, interval);
    printf("\nSleeping for %.2f seconds\n", sleep_time);
    usleep(sleep_time * 1000000);
  }
  stop_threads();
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
    for (auto &m: kv.second->get_ranges())
      idle_ranges.push_back(m);

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

static inline float tv_secs(struct timeval& t1, struct timeval& t2)
{
  return  (t2.tv_sec  - t1.tv_sec) +
          (t2.tv_usec - t1.tv_usec) * 0.000001;
}

void GlobalScan::walk_multi()
{
  struct timeval ts1, ts2;
  float elapsed;

  for (auto& m: idle_ranges)
    m->prepare_walks(MAX_WALKS);

  printf("\nStarting page table scans:\n");
  printf("%7s  %8s  %23s  %23s  %15s\n", "nr_scan", "interval", "young", "top hot", "all");
  printf("====================================================================================\n");

  for (nr_walks = 0; nr_walks < MAX_WALKS;)
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
  ProcIdlePages::reset_sys_refs_count(nr_walks);

  for (auto& m: idle_ranges)
    m->count_refs();

  ProcIdlePages::save_counts(option.output_file);
}

// similar to ProcIdlePages::should_stop()
bool GlobalScan::should_stop_walk()
{
  // page_refs.get_top_bytes() is 0 when nr_walks == 1
  if (nr_walks <= 2)
    return false;

  return 2 * 100 * top_bytes < dram_free_anon_bytes;
}

void GlobalScan::update_dram_free_anon_bytes()
{
  if (option.dram_percent) {
    dram_free_anon_bytes = option.dram_percent * all_bytes;
  } else {
    ProcVmstat proc_vmstat;
    dram_free_anon_bytes = proc_vmstat.anon_capacity();
  }
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
      work_queue.push(job);
      printd("push job %d\n", nr);
      ++nr;
  }

  for (; nr; --nr)
  {
    printd("wait walk job %d\n", nr);
    job = done_queue.pop();
    job.migration->gather_walk_stats(young_bytes, top_bytes, all_bytes);
  }

  update_dram_free_anon_bytes();

  printf("%7d  %8.2f  %'15lu %6.2f%%  %'15lu %6.2f%%  %'15lu\n",
         nr_walks,
         (double)interval,
         young_bytes >> 10, 100.0 * young_bytes / all_bytes,
         top_bytes >> 10, 100.0 * top_bytes / all_bytes,
         all_bytes >> 10);
}

void GlobalScan::consumer_loop()
{
  printd("consumer_loop started\n");
  for (;;)
  {
    Job job = work_queue.pop();
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
      return;
    }
    printd("consumer_loop done job\n");
    done_queue.push(job);
  }
}

void GlobalScan::migrate()
{
  int nr = 0;
  Job job;
  job.intent = JOB_MIGRATE;

  for (auto& m: idle_ranges)
  {
      job.migration = m;
      work_queue.push(job);
      ++nr;
  }

  for (; nr; --nr)
  {
    printd("wait migrate job %d\n", nr);
    job = done_queue.pop();
  }

  ProcVmstat proc_vmstat;
  proc_vmstat.show_numa_stats();
}

void GlobalScan::update_interval(bool finished)
{
  if (option.interval)
    return;

  if (nr_walks <= 1)
    return;

  const int div = 66; // the smaller than 100, the more real nr_walks will be
                      // in order to bring top_bytes down to dram_percent/2
  float ratio = dram_free_anon_bytes / (div * young_bytes + 1.0);
  if (ratio > 10)
    ratio = 10;
  else if (ratio < 0.2)
    ratio = 0.2;

  printd("interval %f real %f * %.1f for young %.2f%%\n",
         (double) interval,
         (double) real_interval,
         (double) ratio,
         (double) 100 * young_bytes / all_bytes);

  interval = real_interval * ratio;
  if (interval < 0.000001)
    interval = 0.000001;

  if (finished && nr_walks < MAX_WALKS / 4) {
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
  }
}
