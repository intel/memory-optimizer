/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

#include "lib/debug.h"
#include "Process.h"
#include "ProcMaps.h"
#include "ProcStatus.h"
#include "EPTMigrate.h"

extern Option option;

int Process::load(pid_t n)
{
  pid = n;
  return proc_status.load(pid);
}

void Process::add_range(unsigned long start, unsigned long end)
{
  std::shared_ptr<EPTMigrate> p;

  p = std::make_shared<EPTMigrate>();
  p->set_pid(pid);
  p->set_va_range(start, end);
  idle_ranges.push_back(p);

  printdd("pid=%d add_range %lx-%lx=%lx\n", pid, start, end, end - start);
}

int Process::split_ranges()
{
  unsigned long rss_anon = proc_status.get_number("RssAnon") << 10;
  unsigned long max_bytes = option.split_rss_size;

  if(option.hugetlb)
    rss_anon = proc_status.get_number("HugetlbPages") << 10;

  if (rss_anon <= 0)
    return 0;

  if (max_bytes == 0)
    max_bytes = TASK_SIZE_MAX;

  if (rss_anon < max_bytes) {
    add_range(0, TASK_SIZE_MAX);
    return 0;
  }

  unsigned long sum = 0;
  unsigned long start = 0;
  unsigned long end;
  auto vmas = proc_maps.load(pid);

  for (auto& vma: vmas) {

    if (vma.start >= TASK_SIZE_MAX)
      continue;

    unsigned long vma_size = vma.end - vma.start;
    unsigned long offset;
    sum += vma_size;
    for (offset = max_bytes; offset < vma_size; offset += max_bytes) {
      end = (vma.start + offset) & ~(PMD_SIZE - 1);
      add_range(start, end);
      start = end;
      sum = 0;
    }

    if (sum > max_bytes) {
      end = vma.end;
      add_range(start, end);
      start = end;
      sum = 0;
    }
  }

  if (sum)
    add_range(start, TASK_SIZE_MAX);

  return 0;
}

void Process::set_policy(Policy* pol)
{
  policy = *pol;

  // pass policy to all VMAs
  for (auto &migration: idle_ranges)
    migration->set_policy(*pol);
}

bool Process::match_policy(Policy& policy)
{
  if (policy.pid >= 0) {
    if (policy.pid == pid)
      return true;
  }

  if (!policy.name.empty()) {
    if (!policy.name.compare(proc_status.get_name())) {
      printd("find policy for %s\n", policy.name.c_str());
      return true;
    }
  }

  return false;
}

Policy* Process::match_policies(PolicySet& policies)
{
    for (Policy &policy: policies)
      if (match_policy(policy))
        return &policy;

    return NULL;
}


int ProcessCollection::collect()
{
  int err;

  proccess_hash.clear();

  err = pids.collect();
  if (err)
    return err;

  for (pid_t pid: pids.get_pids())
  {
    auto p = std::make_shared<Process>();

    err = p->load(pid);
    if (err)
      continue;

    err = p->split_ranges();
    if (err)
      continue;

    proccess_hash[pid] = p;
  }

  return 0;
}

int ProcessCollection::collect(PolicySet& policies)
{
  int err;

  proccess_hash.clear();

  err = pids.collect();
  if (err)
    return err;

  for (pid_t pid: pids.get_pids()) {
    auto p = std::make_shared<Process>();

    err = p->load(pid);
    if (err)
      continue;

    Policy* policy = p->match_policies(policies);
    if (!policy)
      continue;

    err = p->split_ranges();
    if (err)
      continue;

    p->set_policy(policy);

    proccess_hash[pid] = p;
  }

  return 0;
}

void ProcessCollection::dump()
{
    printf("dump process collection start:\n");
    for (auto &iter: proccess_hash) {
        printf("pid: %5d, name: %s\n",
               iter.first,
               iter.second->proc_status.get_name().c_str());
    }
    printf("dump process collection end.\n");
}
