#ifndef _OPTION_H
#define _OPTION_H

#include <string>
#include <vector>

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
  pid_t Pid;
  std::string Name;
  MigrateWhat migrate_what;
  PlaceWhat place_what;
};

struct Option
{
  Option();
  int set_dram_percent(int dp);
  void dump();
  
public:
  int debug_level;

  static const int DRAM_NUMA_NODE = 0;
  static const int PMEM_NUMA_NODE = 1;

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
