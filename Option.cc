#include "Option.h"

#include <iostream>

Option::Option()
{
  nr_walks = 0; // auto stop when nr_top_pages can fit in half DRAM size
  interval = 0.1;
  migrate_what = MIGRATE_BOTH;
  hot_min_refs = -1;
  cold_max_refs = -1;
}

int Option::set_dram_percent(int dp)
{
  if (dp < 0 || dp > 100) {
    std::cerr << "dram percent out of range [0, 100]" << std::endl;
    return -ERANGE;
  }

  dram_percent = dp;
  return 0;
}

