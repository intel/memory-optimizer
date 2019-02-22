/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
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
