/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

#ifndef AEP_PROCESS_H
#define AEP_PROCESS_H

#include <memory>
#include <vector>
#include <unordered_map>

#include "ProcPid.h"
#include "ProcMaps.h"
#include "ProcStatus.h"
#include "Option.h"

class EPTMigrate;
typedef std::vector<std::shared_ptr<EPTMigrate>> IdleRanges;

class Process
{
  public:
    int load(pid_t n);
    int split_ranges();
    IdleRanges& get_ranges() { return idle_ranges; }

  private:
    void add_range(unsigned long start, unsigned long end);

  public:
    pid_t      pid;
    ProcStatus proc_status;
    ProcMaps   proc_maps;
    IdleRanges idle_ranges;
};

typedef std::unordered_map<pid_t, std::shared_ptr<Process>> ProcessHash;

class ProcessCollection
{
  public:
    int collect();
    int collect(PolicySet& policies);
    ProcessHash& get_proccesses() { return proccess_hash; }
    void dump();

  private:
    int filter_by_policy(std::shared_ptr<Process> process,
                         Policy &policy);

  private:
    ProcPid pids;
    ProcessHash proccess_hash;
};

#endif
// vim:set ts=2 sw=2 et:
