#ifndef _OPTION_H
#define _OPTION_H

#include <string>

typedef enum {
  MIGRATE_NONE,
  MIGRATE_HOT,
  MIGRATE_COLD,
  MIGRATE_BOTH = MIGRATE_HOT | MIGRATE_COLD,
} MigrateWhat;

struct Option
{
  Option();
  int set_dram_percent(int dp);

public:
  int debug_level;

  pid_t pid;

  float interval;
  int nr_walks;

  // set either dram_percent or hot_min_refs/cold_max_refs, but not both
  int dram_percent;
  int hot_min_refs;
  int cold_max_refs;

  MigrateWhat migrate_what;

  std::string output_file;
};

#endif
// vim:set ts=2 sw=2 et:
