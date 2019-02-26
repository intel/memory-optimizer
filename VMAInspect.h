/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Yao Yuan <yuan.yao@intel.com>
 *          Fengguang Wu <fengguang.wu@intel.com>
 *          Liu Jingqi <jingqi.liu@intel.com>
 */

#ifndef _VMA_INSPECT_H
#define _VMA_INSPECT_H

#include <sys/types.h>
#include "MovePages.h"

struct proc_maps_entry;
class Formatter;
class NumaNodeCollection;

class VMAInspect
{
  public:
    VMAInspect() {};
    ~VMAInspect() {};

    int dump_task_nodes(pid_t i, Formatter* m);
    int dump_vma_nodes(Formatter* m, int is_split_vma,
                       proc_maps_entry& vma, MovePagesStatusCount& status_sum);
    void set_numa_collection(NumaNodeCollection* new_numa_collection) {
      numa_collection = new_numa_collection;
      locator.set_numacollection(new_numa_collection);
    }

    unsigned long get_dram_kb() { return locator.dram_kb; };
    unsigned long get_pmem_kb() { return locator.pmem_kb; };

  private:
    void fill_addrs(std::vector<void *>& addrs, unsigned long start);
    void dump_node_percent(Formatter* fmt, int slot);

  private:
    pid_t pid;
    MovePages locator;
    NumaNodeCollection* numa_collection;
};

#endif
// vim:set ts=2 sw=2 et:
