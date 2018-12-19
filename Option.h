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
  PLACEMENT_DRAM,
  PLACEMENT_AEP,
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


struct Option
{
  Option();

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
  int debug_level;

  static const int DRAM_NUMA_NODE = 0;
  static const int PMEM_NUMA_NODE = 1;

  static std::unordered_map<std::string, bool> bool_name_map;
  static std::unordered_map<std::string, MigrateWhat> migrate_name_map;
  static std::unordered_map<std::string, Placement> placement_name_map;

  pid_t pid;

  float initial_interval;
  float interval;
  float sleep_secs;
  int max_walks;
  int nr_walks;
  int nr_loops;

  // set either dram_percent or hot_min_refs/cold_max_refs, but not both
  int dram_percent;
  int hot_min_refs;
  int cold_max_refs;

  int exit_on_stabilized;
  bool exit_on_exceeded;
  bool dump_options;

  int max_threads;
  unsigned long split_rss_size;

  float bandwidth_mbps;
  MigrateWhat migrate_what;

  std::string output_file;
  std::string config_file;

  NumaHWConfig numa_hw_config;

  int debug_move_pages = 0;

  // Not used for now, so current sys-refs behavior is to ignore all processes
  // w/o a policy defined. In future, may consider applying this to all
  // processes in ProcessCollection::collect().
  // Policy default_policy;

private:
  PolicySet  policies;
};

#endif
// vim:set ts=2 sw=2 et:
