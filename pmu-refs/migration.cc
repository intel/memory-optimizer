/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
 */

#include <cstdio>
#include <cerrno>

#include <numa.h>
#include <numaif.h>

#include "hmd-common.h"
#include "hmd-config.h"
#include "migration.h"
#include "cmsk.h"
#include "Numa.h"
#include "pmu.h"
#include "AddressRangeFilter.h"

static inline int do_move_pages(int pid, unsigned long count,
                                std::vector<unsigned long>& pages,
                                const std::vector<int>* nodes,
                                std::vector<int>& status,
                                int flags)
{
  int ret;

  ret = move_pages(pid, count, (void **)pages.data(),
                   nodes ? nodes->data() : NULL,
                   status.data(), flags);
  if (ret < 0) {
    /*
     * It's OK if the process has gone (ESRCH),
     * or process is kernel thread (EINVAL)
     */
    if (errno != ESRCH && errno != EINVAL) {
      sys_err("failed to move pages");
    }
    return ret;
  }

  return 0;
}

void MigrationState::init(PmuState *pmu_state,
			  NumaNodeCollection *numa_nodes,
			  AddressRangeFilter *filter)
{
  hot_pages_.resize(hmd_config.move_pages_max);
  sample_counts_.resize(hmd_config.move_pages_max);
  target_nodes_.resize(hmd_config.move_pages_max);
  migrate_status_.resize(hmd_config.move_pages_max);
  pmu_state_ = pmu_state;
  numa_nodes_ = numa_nodes;
  arfilter_ = filter;
}

int MigrationState::get_current_nodes()
{
  return do_move_pages(pid_, len_, hot_pages_, nullptr,
                       migrate_status_, MPOL_MF_MOVE_ALL);
}

void MigrationState::get_target_nodes()
{
  int i, j = 0, nid;
  NumaNode *node;

  for (i = 0; i < len_; i++) {
    nid = migrate_status_[i];
    if (!numa_nodes_->is_valid_nid(nid))
      continue;
    node = numa_nodes_->get_node(nid);
    if (!node->is_pmem())
      continue;
    hot_pages_[j] = hot_pages_[i];
    sample_counts_[j] = sample_counts_[i];
    target_nodes_[j] = node->promote_target->id();
    j++;
  }
  len_ = j;
}

bool MigrationState::is_page_hot_in_target_dram_node(
    int sample_count, int target_node)
{
  return sample_count * pmu_state_->get_pmem_sample_period() >=
      pmu_state_->get_node(target_node)->get_dram_count_avg() *
      hmd_config.dram_count_multiple;
}

void MigrationState::filter()
{
  int i, j = 0, node, sample_count;

  for (i = 0; i < len_; i++) {
    node = target_nodes_[i];
    sample_count = sample_counts_[i];
    if (/* !numa_nodes_->get_node(node)->mem_watermark_ok && */
            !is_page_hot_in_target_dram_node(sample_count, node))
      continue;
    sample_counts_[j] = sample_count;
    hot_pages_[j] = hot_pages_[i];
    target_nodes_[j] = target_nodes_[i];
    j++;
  }
  len_ = j;
}

int MigrationState::__move_pages()
{
  int i, ret, moved = 0;

  if (!pid_ || !len_)
    return 0;

  ret = get_current_nodes();
  if (ret < 0)
    return 0;
  get_target_nodes();
  filter();
  if (!len_)
    return 0;

  ret = do_move_pages(pid_, len_, hot_pages_, &target_nodes_,
                      migrate_status_, MPOL_MF_MOVE_ALL);
  if (ret < 0)
    return 0;

  for (i = 0; i < len_; i++) {
    if (migrate_status_[i] == target_nodes_[i])
      moved++;
    else {
      arfilter_->insert_range(pid_, hot_pages_[i], PAGE_SIZE);
      if (hmd_config.verbose)
        printf("Failed to move page 0x%lx of pid %d\n", hot_pages_[i], pid_);
    }
  }

  return moved;
}

int MigrationState::move_pages(int pid,
                               const std::vector<unsigned long>& hot_pages,
                               const std::vector<int>& sample_counts)
{
  pid_ = pid;
  len_ = hot_pages.size();
  std::copy(hot_pages.begin(), hot_pages.end(), hot_pages_.begin());
  std::copy(sample_counts.begin(), sample_counts.end(), sample_counts_.begin());

  return __move_pages();
}
