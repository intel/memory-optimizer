/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

#include "Option.h"
#include "lib/stats.h"
#include <iostream>

std::unordered_map<std::string, bool> Option::bool_name_map = {
  {"true",  true},
  {"yes",   true},
  {"false", false},
  {"no",    false},
};

std::unordered_map<std::string, MigrateWhat> Option::migrate_name_map = {
  {"none", MIGRATE_NONE},
  {"hot",  MIGRATE_HOT},
  {"cold", MIGRATE_COLD},
  {"both", MIGRATE_BOTH},
};

std::unordered_map<std::string, Placement> Option::placement_name_map = {
  {"none", PLACEMENT_NONE},
  {"dram", PLACEMENT_DRAM},
  {"pmem", PLACEMENT_PMEM},
};

int Option::set_dram_percent(int dp)
{
  if (dp < 0 || dp > 100) {
    std::cerr << "dram percent out of range [0, 100]" << std::endl;
    return -ERANGE;
  }

  dram_percent = dp;
  return 0;
}

void Option::dump()
{
  printf("option dump begin:\n");
  printf("debug_level = %d\n", debug_level);
  printf("pid = %d\n", pid);
  printf("initial_interval = %f\n", initial_interval);
  printf("interval = %f\n", interval);
  printf("sleep_secs = %f\n", sleep_secs);
  printf("nr_walks = %d\n", nr_walks);
  printf("max_walks = %d\n", max_walks);
  printf("nr_loops = %d\n", nr_loops);
  printf("dram_percent = %d\n", dram_percent);
  printf("exit_on_stabilized = %d\n", exit_on_stabilized);
  printf("exit_on_exceeded = %d\n", (int)exit_on_exceeded);
  printf("dump_options = %d\n", (int)dump_options);
  printf("dump_processes = %d\n", (int)dump_processes);
  printf("hot_min_refs = %d\n", hot_min_refs);
  printf("cold_max_refs = %d\n", cold_max_refs);
  printf("max_threads = %d\n", max_threads);
  printf("split_rss_size = %s\n", split_rss_size.c_str());
  printf("bandwidth_mbps = %g\n", bandwidth_mbps);
  printf("migrate_what = %d\n", migrate_what);
  printf("output_file = %s\n", output_file.c_str());
  printf("config_file = %s\n", config_file.c_str());
  printf("daemon = %d\n", (int)daemon);
  printf("exit_on_converged = %d\n", (int)exit_on_converged);

  for (size_t i = 0; i < policies.size(); ++i) {
      printf("policy %ld:\n", i);
      printf("pid: %d\n", policies[i].pid);
      printf("name: %s\n", policies[i].name.c_str());
      printf("migration: %d\n", policies[i].migrate_what);
      printf("placement: %d\n", policies[i].placement);
      printf("dump_distribution: %d\n", policies[i].dump_distribution);
      printf("\n");
  }

  printf("Numa HW Config V2:\n");
  for(auto &numa_config_item : numa_hw_config_v2) {
    std::string id = "unknown";
    std::string type = "unknown";
    std::string link_to = "unknown";

    find_map(numa_config_item, "id", id);
    find_map(numa_config_item, "type", type);
    find_map(numa_config_item, "promote_to", link_to);
    find_map(numa_config_item, "demote_to", link_to);

    printf("id: %s type: %s linkto: %s\n",
           id.c_str(), type.c_str(), link_to.c_str());
  }

  printf("option dump end.\n");
}

MigrateWhat Option::parse_migrate_name(std::string name)
{
  MigrateWhat ret_val = MIGRATE_NONE;

  parse_name_map(migrate_name_map, name, ret_val, MIGRATE_END);

  return ret_val;
}

int Option::add_policy(Policy& new_policy)
{
  // a policy should have at least pid or name
  if (new_policy.pid < 0
    && new_policy.name.empty()) {
    std::cerr << "invalid policy: no pid and no name" << std::endl;
    return -1;
  }

  policies.push_back(new_policy);

  return 0;
}
