/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

#include <errno.h>
#include "OptionParser.h"


OptionParser::OptionParser()
  : Option()
{
}

OptionParser::~OptionParser()
{
}

int OptionParser::parse_file(std::string filename)
{
  config_file = filename;
  return reparse();
}

int OptionParser::reparse()
{
  int ret_val;
  YAML::Node config;

  try {
    config = YAML::LoadFile(config_file);
  } catch (...) {
    printf("ERROR: exception on loading YAML file %s\n", config_file.c_str());
    return -1;
  }

  try {

    ret_val = parse_option(config["options"]);
    if (ret_val < 0) {
      printf("ERROR: failed to parse options\n");
      return ret_val;
    }

    ret_val = parse_policies(config["policies"]);

    if (dump_options)
      dump();

  } catch (...) {
    ret_val = -1;
    printf("ERROR: exception on parsing options/policies\n");
  }

  return ret_val;
}

template<typename Tval>
int OptionParser::get_value(const YAML::const_iterator  &iter,
                            const char* key_name, Tval &value)
{
  std::string key = iter->first.as<std::string>();
  if (!key.compare(key_name))
  {
    value = iter->second.as<Tval>();
    return 1;
  }

  return 0;
}

int OptionParser::get_value(const YAML::const_iterator  &iter,
                            const char* key_name, YAML::Node &value)
{
  std::string key = iter->first.as<std::string>();
  if (!key.compare(key_name))
  {
    value = iter->second;
    return 1;
  }

  return 0;
}


int OptionParser::parse_option(YAML::Node &&option_node)
{
    if (!option_node)
      return -1;

    if (!option_node.IsMap())
      return -1;

    for (YAML::const_iterator iter = option_node.begin();
         iter != option_node.end();
         ++iter) {
#define OP_GET_VALUE(name, member) if (get_value(iter, name, member)) continue;
      OP_GET_VALUE("max_walks",       max_walks);
      OP_GET_VALUE("interval",        interval);
      OP_GET_VALUE("initial_interval", initial_interval);
      OP_GET_VALUE("sleep",           sleep_secs);
      OP_GET_VALUE("loop",            nr_loops);
      OP_GET_VALUE("max_threads",     max_threads);
      OP_GET_VALUE("split_rss_size",  split_rss_size);
      OP_GET_VALUE("bandwidth_mbps",  bandwidth_mbps);
      OP_GET_VALUE("dram_percent",    dram_percent);
      OP_GET_VALUE("output",          output_file);
      OP_GET_VALUE("hugetlb",         hugetlb);
      OP_GET_VALUE("thp",             thp);
      OP_GET_VALUE("exit_on_stabilized", exit_on_stabilized);
      OP_GET_VALUE("numa_dram_nodes", numa_hw_config.numa_dram_list);
      OP_GET_VALUE("numa_pmem_nodes", numa_hw_config.numa_pmem_list);
      OP_GET_VALUE("numa_peer_nodes", numa_hw_config.pmem_dram_map);
      OP_GET_VALUE("debug_move_pages",    debug_move_pages);
      OP_GET_VALUE("anti_thrash_threshold", anti_thrash_threshold);
      OP_GET_VALUE("one_period_migration_size", one_period_migration_size);
#undef OP_GET_VALUE

      std::string str_val;
#define OP_GET_BOOL_VALUE(name, member, max_val)              \
      if (get_value(iter, name, str_val)) { \
        Option::parse_name_map(bool_name_map, str_val, member, max_val); \
        continue; \
      }

      OP_GET_BOOL_VALUE("dump_options", dump_options, 2);
      OP_GET_BOOL_VALUE("dump_processes", dump_processes, 2);
      OP_GET_BOOL_VALUE("exit_on_exceeded", exit_on_exceeded, 2);
      OP_GET_BOOL_VALUE("daemon", daemon, 2);
      OP_GET_BOOL_VALUE("show_numa_stats", show_numa_stats, 2);
      OP_GET_BOOL_VALUE("exit_on_converged", exit_on_converged, 2);
#undef OP_GET_BOOL_VALUE

      YAML::Node sub_node;
      if (get_value(iter, "numa_nodes", sub_node)) {
        parse_numa_nodes(sub_node);
        continue;
      }

      // parse_common_policy(iter, default_policy);
    }

    return 0;
}

void OptionParser::parse_numa_nodes(YAML::Node &numa_nodes)
{
  std::string id;
  YAML::Node node_entry;
  NumaHWConfigEntry new_config_entry;

  for (auto iter = numa_nodes.begin();
       iter != numa_nodes.end();
       ++iter) {
    new_config_entry.clear();

    id = iter->first.as<std::string>();
    node_entry = iter->second;

    parse_one_numa_node(node_entry, new_config_entry);
    new_config_entry["id"] = id;

    numa_hw_config_v2.push_back(new_config_entry);
  }
}

void OptionParser::parse_one_numa_node(YAML::Node &one_numa_node,
                                       NumaHWConfigEntry &one_entry)
{
  for (auto iter = one_numa_node.begin();
       iter != one_numa_node.end();
       ++iter)
    one_entry[iter->first.as<std::string>()]
      = iter->second.as<std::string>();
}

int OptionParser::parse_policies(YAML::Node &&policies_node)
{
    if (!policies_node)
      return -1;

    if (!policies_node.IsSequence())
      return -1;

    for (std::size_t i = 0; i < policies_node.size(); ++i) {
      if (!policies_node[i].IsMap())
        continue;

      parse_one_policy(policies_node[i]);
    }

    return 0;
}

void OptionParser::parse_common_policy(const YAML::const_iterator& iter, Policy& policy)
{
  std::string str_val;

  if (get_value(iter, "migration", str_val)) {
    policy.migrate_what
      = Option::parse_migrate_name(str_val);
    return;
  }

  if (get_value(iter, "placement", str_val)) {
    Option::parse_name_map(placement_name_map, str_val, policy.placement, PLACEMENT_END);
    return;
  }

  if (get_value(iter, "dump_distribution", str_val)) {
    Option::parse_name_map(bool_name_map, str_val, policy.dump_distribution, 2);
    return;
  }
}

void OptionParser::parse_one_policy(YAML::Node &&policy_node)
{
    struct Policy new_policy;

    for (auto iter = policy_node.begin();
         iter != policy_node.end();
         ++iter) {
      if (get_value(iter, "pid", new_policy.pid))
        continue;
      if (get_value(iter, "name", new_policy.name))
        continue;

      parse_common_policy(iter, new_policy);
    }

    add_policy(new_policy);
}
