/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

#ifndef _OPTION_H
#define _OPTION_H

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

typedef enum {
  MIGRATE_NONE,
  MIGRATE_HOT,
  MIGRATE_COLD,
  MIGRATE_BOTH = MIGRATE_HOT | MIGRATE_COLD,
  MIGRATE_END,
} MigrateWhat;


typedef enum {
  PLACEMENT_NONE,
  PLACEMENT_DRAM,   // skip scan, assuming mlock'ed in DRAM
  PLACEMENT_PMEM,   // no effect for now
  PLACEMENT_END,
} Placement;


struct Policy
{
  Policy() {
    pid = -1;
    migrate_what = MIGRATE_NONE;
    placement = PLACEMENT_NONE;
    dump_distribution = false;
  }

  pid_t pid;
  std::string name;
  MigrateWhat migrate_what;
  Placement placement;
  bool dump_distribution;
};
typedef std::vector<Policy> PolicySet;


struct NumaHWConfig{
  bool is_valid() {
    return numa_dram_list.size()
           || numa_pmem_list.size()
           || pmem_dram_map.size();
  }
  std::string numa_dram_list;
  std::string numa_pmem_list;
  std::string pmem_dram_map;
};

typedef std::unordered_map<std::string, std::string> NumaHWConfigEntry;
typedef std::vector<NumaHWConfigEntry> NumaHWConfigV2;

struct Option
{
  int set_dram_percent(int dp);

  int add_policy(Policy& new_policy);
  PolicySet& get_policies() {
    return policies;
  }

  void dump();

  static MigrateWhat parse_migrate_name(std::string name);

  template<typename Tmap, typename Tval>
  static int parse_str_from_map(Tmap& map, std::string &name, Tval& val)
  {
    auto search = map.find(name);
    if (search != map.end()) {
      val = search->second;
      return 0;
    }
    return -1;
  }

  template<typename Tmap, typename Tval>
  static int parse_name_map(Tmap& map, std::string name, Tval& val, int max_val)
  {
    if (isdigit(name[0])) {
      int m = atoi(name.c_str());
      if (m >= max_val) {
        std::cerr << "invalid value: " << name << std::endl;
        return -1;
      }
      val = (Tval) m;
      return 0;
    }

    if (parse_str_from_map(map, name, val) < 0) {
      std::cerr << "invalid value: " << name << std::endl;
      return -2;
    }

    return 0;
  }

public:
  int debug_level = 0;

  static const int DRAM_NUMA_NODE = 0;
  static const int PMEM_NUMA_NODE = 1;

  static std::unordered_map<std::string, bool> bool_name_map;
  static std::unordered_map<std::string, MigrateWhat> migrate_name_map;
  static std::unordered_map<std::string, Placement> placement_name_map;

  pid_t pid = -1;

  float initial_interval = 0.1;
  float interval = 0; // auto adjust
  float sleep_secs = 1;
  int max_walks = 10;
  int nr_walks = 0; // auto stop when nr_top_pages can fit in half DRAM size
  int nr_loops = 0;

  // set either dram_percent or hot_min_refs/cold_max_refs, but not both
  int dram_percent = 0;
  int hot_min_refs = -1;
  int cold_max_refs = -1;

  int exit_on_stabilized = 0; // percent moved
  bool exit_on_exceeded = false; // when exceed dram_percent
  bool dump_options = false;
  bool dump_processes = false;

  int hugetlb = 0;
  int thp = 0;

  int max_threads = 0;
  std::string split_rss_size; // no split task address space

  float bandwidth_mbps = 0;
  MigrateWhat migrate_what = MIGRATE_HOT;

  std::string output_file;
  std::string config_file;

  NumaHWConfig numa_hw_config;
  NumaHWConfigV2 numa_hw_config_v2;

  int debug_move_pages = 0;

  bool daemon = false;
  bool show_numa_stats = false;
  // Not used for now, so current sys-refs behavior is to ignore all processes
  // w/o a policy defined. In future, may consider applying this to all
  // processes in ProcessCollection::collect().
  // Policy default_policy;

  int anti_thrash_threshold = 2;

private:
  PolicySet  policies;
};

#endif
// vim:set ts=2 sw=2 et:
