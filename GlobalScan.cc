
#include <thread>
#include <atomic>
#include <iostream>
#include <unistd.h>

#include "lib/debug.h"
#include "GlobalScan.h"

GlobalScan::GlobalScan()
{
}

void GlobalScan::main_loop()
{
  int nloop = option.nr_loops;

  create_threads();
  for (; !option.nr_loops || nloop-- > 0;)
  {
    collect();
    walk_multi();
    migrate();
    count_refs();
    update_interval();

    usleep(option.sleep_secs * 1000000);
  }
  stop_threads();
}

int GlobalScan::collect()
{
	int err;

	err = process_collection.collect();
	if (err)
		return err;

	idle_ranges.clear();

	for (auto &kv: process_collection.get_proccesses())
    for (auto &m: kv.second->get_ranges())
      idle_ranges.push_back(m);

  return 0;
}

void GlobalScan::create_threads()
{
  worker_threads.reserve(NR_THREADS);

  for (int i = 0; i < NR_THREADS; ++i)
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
  top_bytes = 0;
  all_bytes = 0;

  for (auto& m: idle_ranges)
    m->prepare_walks(MAX_WALKS);

  for (nr_walks = 0; nr_walks < MAX_WALKS; ++nr_walks)
  {
    walk_once();

    if (should_stop_walk())
      break;
  }
}

void GlobalScan::count_refs()
{
  ProcIdlePages::reset_sys_refs_count();

  for (auto& m: idle_ranges)
    m->count_refs();

  ProcIdlePages::save_counts(option.output_file);
}

// similar to ProcIdlePages::should_stop()
bool GlobalScan::should_stop_walk()
{
  if (!option.dram_percent)
    return false;

  // page_refs.get_top_bytes() is 0 when nr_walks == 1
  if (nr_walks <= 2)
    return false;

  return 2 * 100 * top_bytes < option.dram_percent * all_bytes;
}

void GlobalScan::walk_once()
{
  int nr = 0;
  Job job;
  job.intent = JOB_WALK;

  for (auto& m: idle_ranges)
  {
      job.migration = m;
      work_queue.push(job);
      printd("push job %d\n", nr);
      ++nr;
  }

  usleep(option.interval * 1000000);

  for (; nr; --nr)
  {
    printd("wait walk job %d\n", nr);
    job = done_queue.pop();
    account(job.migration);
  }
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
      do_walk(job.migration);
      break;
    case JOB_MIGRATE:
      do_migrate(job.migration);
      break;
    case JOB_QUIT:
      printd("consumer_loop quit job\n");
      return;
    }
    printd("consumer_loop done job\n");
    done_queue.push(job);
  }
}

void GlobalScan::do_walk(MigrationPtr migration)
{
    migration->walk();
}

void GlobalScan::do_migrate(MigrationPtr migration)
{
    migration->migrate();
}

void GlobalScan::account(MigrationPtr migration)
{
  migration->get_sum(top_bytes, all_bytes);
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
    account(job.migration);
  }
}

void GlobalScan::update_interval()
{
}

