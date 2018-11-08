#include "Option.h"

#include <iostream>

Option::Option()
{
  nr_loops = 0;
  nr_walks = 0; // auto stop when nr_top_pages can fit in half DRAM size
  interval = 0; // auto adjust
  sleep_secs = 1;
  migrate_what = MIGRATE_HOT;
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


void Option::dump()
{
  printf("option dump begin:\n");
  printf("debug_level = %d\n", debug_level);
  printf("pid = %d\n", pid);
  printf("inteval = %f\n", interval);
  printf("sleep_secs = %f\n", sleep_secs);
  printf("nr_walks = %d\n", nr_walks);
  printf("nr_loops = %d\n", nr_loops);
  printf("dram_percent = %d\n", dram_percent);
  printf("hot_min_refs = %d\n", hot_min_refs);
  printf("cold_max_refs = %d\n", cold_max_refs);
  printf("bandwidth_mbps = %lu\n", bandwidth_mbps);
  printf("output_file = %s\n", output_file.c_str());
  printf("config_file = %s\n", config_file.c_str());

  printf("option dump end.\n");
 
}
