/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

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

#ifndef __NUMA__HH__
#define __NUMA__HH__

#include <vector>
#include <iterator>
#include <map>
#include <memory>

#include <numa.h>
#include <numaif.h>

#include "Option.h"
#include "lib/debug.h"

typedef std::map<const char*, std::string> NodeInfo;
typedef std::vector<NodeInfo> NumaInfo;

enum numa_node_type {
  NUMA_NODE_DRAM,
  NUMA_NODE_PMEM,
  NUMA_NODE_END,
};

class NumaNode {
  int id_;
  enum numa_node_type type_;
  long mem_total_;
  long mem_free_;
  bool mem_watermark_ok_;

public:

  // possible to a union here?
  NumaNode *promote_target;
  NumaNode *demote_target;

  NumaNode(int nid, enum numa_node_type node_type) :
    id_(nid), type_(node_type),
    promote_target(NULL), demote_target(NULL) {}

  int id(void) { return id_; }
  enum numa_node_type type(void) { return type_; }
  bool is_pmem(void) { return type_ == NUMA_NODE_PMEM; }

  void collect_meminfo(void)
  {
    mem_total_ = numa_node_size(id_, &mem_free_);
    if (mem_total_ == -1)
      err("numa_node_size");
  }

  void check_watermark(int watermark_percent)
  {
    mem_watermark_ok_ = mem_free_ >
      mem_total_ * watermark_percent / 100;
  }

  bool get_mem_watermark_ok() { return mem_watermark_ok_; }

  unsigned long mem_used(void)
  {
    return mem_total_ - mem_free_;
  }

  void set_peer_node(NumaNode* peer_node)
  {
    switch (type_) {
      case NUMA_NODE_DRAM:
        demote_target = peer_node;
        break;
      case NUMA_NODE_PMEM:
        promote_target = peer_node;
        break;
      default:
        break;
    }
  }

  NumaNode* get_peer_node()
  {
    switch (type_) {
      case NUMA_NODE_DRAM:
        return demote_target;
      case NUMA_NODE_PMEM:
        return promote_target;
      default:
        return NULL;
    }
  }
};

class NumaNodeCollection
{
  int nr_possible_node_;
  int nr_possible_cpu_;

  /* map from cpu No. to node id */
  std::vector<int> cpu_node_map_;

  /* map from node id to NumaNode*, and memory manager */
  std::vector<std::unique_ptr<NumaNode>> node_map_;

  std::vector<NumaNode *> nodes_;
  std::vector<NumaNode *> dram_nodes_;
  std::vector<NumaNode *> pmem_nodes_;

  void init_cpu_map(void);

public:
  using iterator = std::vector<NumaNode *>::iterator;

  void collect(NumaHWConfig *numa_option,
               NumaHWConfigV2 *numa_option_v2);
  void collect_dram_nodes_meminfo(void);
  void check_dram_nodes_watermark(int watermark_percent);
  int get_node_lowest_cpu(int node);

  int nr_possible_node(void)
  {
    return nr_possible_node_;
  }

  NumaNode *get_node(int nid)
  {
    return node_map_.at(nid).get();
  }

  NumaNode& operator[](int nid)
  {
    return *get_node(nid);
  }

  iterator begin(void)
  {
    return nodes_.begin();
  }

  iterator end(void)
  {
    return nodes_.end();
  }

  const std::vector<NumaNode *>& get_all_nodes() { return nodes_; }
  const std::vector<NumaNode *>& get_dram_nodes() { return dram_nodes_; }
  const std::vector<NumaNode *>& get_pmem_nodes() { return pmem_nodes_; }

  NumaNode *node_of_cpu(int cpu)
  {
    return get_node(cpu_node_map_.at(cpu));
  }

  bool is_valid_nid(int nid)
  {
    return nid >= 0 && nid < nr_possible_node_ && node_map_[nid];
  }

  void dump();

private:
    void collect_by_config(NumaHWConfig *numa_option);
    void collect_by_config(NumaHWConfigV2 *numa_option);
    void collect_by_sysfs(void);

    int parse_sysfs_per_node(int node_id, NodeInfo& node_info);
    int parse_field(const char* field_name, std::string &value);

    template<typename T>
    numa_node_type get_node_type(T& map, std::string &type_str);

    int load_numa_info(NumaInfo& numa_info, int node_count);
    int create_node_objects(NumaInfo& numa_info);
    int create_node(int node_id, numa_node_type type);
    void setup_node_relationship(NumaInfo& numa_info, bool is_bidir);
    void set_target_node(int node_id, int target_node_id, bool is_bidir);
    void set_default_target_node();

    int get_node_id(NumaHWConfigEntry& entry);
    int get_node_linkto(NumaHWConfigEntry& entry);
    numa_node_type get_hwconfig_node_type(NumaHWConfigEntry& entry);
    int create_node_objects(NumaHWConfigV2& numa_hw_config);
    void setup_node_relationship(NumaHWConfigV2& numa_hw_config, bool is_bidir);
};

#endif /* __NUMA__HH__ */
// vim:set ts=2 sw=2 et:
