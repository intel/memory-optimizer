/*
 * Copyright (c) 2018 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __MIGRATION__HH__
#define __MIGRATION__HH__

#include <vector>

class PmuState;
class NumaNodeCollection;
struct cmsk;
class AddressRangeFilter;

class MigrationState
{
  int pid_;
  int len_;

  std::vector<unsigned long> hot_pages_;
  std::vector<int> sample_counts_;
  std::vector<int> target_nodes_;
  std::vector<int> migrate_status_;

  PmuState *pmu_state_;
  NumaNodeCollection *numa_nodes_;
  AddressRangeFilter *arfilter_;

  int get_current_nodes();
  void get_target_nodes();
  bool is_page_hot_in_target_dram_node(int sample_count, int target_node);
  void filter();
  int __move_pages();

 public:
  void init(PmuState *pmu_state, NumaNodeCollection *numa_nodes,
	    AddressRangeFilter *filter);
  int move_pages(int pid, const std::vector<unsigned long>& hot_pages,
                 const std::vector<int>& sample_counts);
};

#endif /* __MIGRATION__HH__ */
