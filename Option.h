#ifndef _OPTION_H
#define _OPTION_H

#include <string>
#include <vector>
#include <unordered_map>
typedef enum {
  MIGRATE_NONE,
  MIGRATE_HOT,
  MIGRATE_COLD,
  MIGRATE_BOTH = MIGRATE_HOT | MIGRATE_COLD,
} MigrateWhat;


typedef enum {
  PLACEMENT_NONE,
  PLACEMENT_DRAM,
  PLACEMENT_AEP,
  PLACEMENT_END,
} PlaceWhat;


struct Policy
{
  Policy() {
    pid = -1;
    migrate_what = MIGRATE_NONE;
    place_what = PLACEMENT_NONE;
  }

  pid_t pid;
  std::string name;
  MigrateWhat migrate_what;
  PlaceWhat place_what;
};


struct Option
{
  Option();

  int set_dram_percent(int dp);
  void dump();

  static MigrateWhat parse_migrate_name(std::string name);
  static PlaceWhat parse_placement_name(std::string name);

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

public:
  int debug_level;

  static const int DRAM_NUMA_NODE = 0;
  static const int PMEM_NUMA_NODE = 1;

  static std::unordered_map<std::string, MigrateWhat> migrate_name_map;
  static std::unordered_map<std::string, PlaceWhat> placement_name_map;

  pid_t pid;

  float interval;
  float sleep_secs;
  int nr_walks;
  int nr_loops;

  // set either dram_percent or hot_min_refs/cold_max_refs, but not both
  int dram_percent;
  int hot_min_refs;
  int cold_max_refs;

  unsigned long bandwidth_mbps;
  MigrateWhat migrate_what;

  std::string output_file;
  std::string config_file;

  std::vector<Policy>  policies;
};


extern Option option;


#endif
// vim:set ts=2 sw=2 et:
