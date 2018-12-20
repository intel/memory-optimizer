/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

#ifndef AEP_PROC_VMSTAT_H
#define AEP_PROC_VMSTAT_H

// interface to /proc/vmstat and /sys/devices/system/node/node*/vmstat

#include <string>
#include <vector>
#include <unordered_map>

typedef std::unordered_map<std::string, unsigned long> vmstat_map;

class NumaNodeCollection;
class ProcVmstat
{
  public:
    int load_vmstat();
    int load_numa_vmstat();
    void clear() { proc_vmstat.clear(); numa_vmstat.clear(); };

    unsigned long vmstat(std::string name);
    unsigned long vmstat(int nid, std::string name);
    unsigned long vmstat(std::vector<int>& nid);

    const std::vector<vmstat_map>& get_numa_vmstat() { return numa_vmstat; }
    const             vmstat_map & get_proc_vmstat() { return proc_vmstat; }

    unsigned long anon_capacity();
    unsigned long anon_capacity(int nid);

    void show_numa_stats(NumaNodeCollection* numa_collection);

  private:
    vmstat_map __load_vmstat(const char *path);

  private:
    std::vector<vmstat_map> numa_vmstat;
    vmstat_map proc_vmstat;
};

#endif
// vim:set ts=2 sw=2 et:
