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
  long mem_total;
  long mem_free;

public:
  bool mem_watermark_ok;

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
    mem_total = numa_node_size(id_, &mem_free);
    if (mem_total == -1)
      err("numa_node_size");
  }

  void check_watermark(int watermark_percent)
  {
    mem_watermark_ok = mem_free >
      mem_total * watermark_percent / 100;
  }

  unsigned long mem_used(void)
  {
    return mem_total - mem_free;
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
  std::vector<int> cpu_node_map;

  /* map from node id to NumaNode* */
  std::vector<NumaNode *> node_map;
  std::vector<NumaNode *> nodes;
  std::vector<NumaNode *> dram_nodes;
  std::vector<NumaNode *> pmem_nodes;

  void init_cpu_map(void);

public:
  using iterator = std::vector<NumaNode *>::iterator;

  void collect(NumaHWConfig *numa_option = NULL);
  void collect_dram_nodes_meminfo(void);
  void check_dram_nodes_watermark(int watermark_percent);
  int get_node_lowest_cpu(int node);

  int nr_possible_node(void)
  {
    return nr_possible_node_;
  }

  NumaNode *get_node(int nid)
  {
    return node_map.at(nid);
  }

  NumaNode& operator[](int nid)
  {
    return *get_node(nid);
  }

  iterator begin(void)
  {
    return nodes.begin();
  }

  iterator end(void)
  {
    return nodes.end();
  }

  const std::vector<NumaNode *>& get_all_nodes() { return nodes; }
  const std::vector<NumaNode *>& get_dram_nodes() { return dram_nodes; }
  const std::vector<NumaNode *>& get_pmem_nodes() { return pmem_nodes; }

  NumaNode *node_of_cpu(int cpu)
  {
    return get_node(cpu_node_map.at(cpu));
  }

  bool is_valid_nid(int nid)
  {
    return nid >= 0 && nid < nr_possible_node_ && node_map[nid];
  }

  void dump();

private:
    void collect_by_config(NumaHWConfig *numa_option);
    void collect_by_sysfs(void);

    int parse_sysfs_per_node(int node_id, NodeInfo& node_info);
    int parse_field(const char* field_name, std::string &value);

    numa_node_type get_numa_type(std::string &type_str);

    int load_numa_info(NumaInfo& numa_info, int node_count);
    int create_node_objects(NumaInfo& numa_info);
    int create_node(int node_id, numa_node_type type);
    void setup_node_relationship(NumaInfo& numa_info, bool is_bidir);
    void set_target_node(int node_id, int target_node_id, bool is_bidir);
    void set_default_target_node();
};

#endif /* __NUMA__HH__ */
// vim:set ts=2 sw=2 et:
