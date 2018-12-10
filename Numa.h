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
#include <numa.h>
#include <numaif.h>

#include "Option.h"
#include "lib/debug.h"

typedef std::map<const char*, std::string> NodeInfo;
typedef std::vector<NodeInfo> NumaInfo;

/*
 * BaseIterator is iterator for container with element type "T *", so
 * acts like "T **".  And container elements may be NULL.
 * DerefIterator will skip NULL elements automatically and acts like
 * "T *".
 */
template<class BaseIterator>
using DerefValueType = typename std::remove_pointer<typename BaseIterator::value_type>::type;

template<class BaseIterator>
class DerefIterator :
    public std::iterator<std::input_iterator_tag, DerefValueType<BaseIterator>>
{
  BaseIterator it_curr;
  BaseIterator it_end;

public:
  using value_type = DerefValueType<BaseIterator>;

  DerefIterator(const DerefIterator<BaseIterator>& ait) :
    it_curr(ait.it_curr), it_end(ait.it_end) {}
  DerefIterator(const BaseIterator& curr, const BaseIterator& end) :
    it_curr(curr), it_end(end) {}
  DerefIterator(void) {}
  bool operator==(const DerefIterator<BaseIterator>& ait)
  {
    return it_curr == ait.it_curr;
  }
  bool operator!=(const DerefIterator<BaseIterator>& ait)
  {
    return it_curr != ait.it_curr;
  }
  DerefIterator<BaseIterator>& skip_null(void)
  {
    while (it_curr != it_end && !*it_curr)
      it_curr++;
    return *this;
  }
  DerefIterator<BaseIterator>& operator++(void)
  {
    it_curr++;
    return skip_null();
  }
  DerefIterator<BaseIterator>& operator++(int)
  {
    it_curr++;
    return skip_null();
  }
  value_type& operator*() { return **it_curr; }
  value_type *operator->() { return *it_curr; }
};

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
  int max_node;
  int nr_cpu;

  /* map from cpu No. to the numa node */
  std::vector<int> cpu_node_map;

  std::vector<NumaNode *> nodes;
  std::vector<NumaNode *> dram_nodes;
  std::vector<NumaNode *> pmem_nodes;

  void init_cpu_map(void);

public:
  typedef DerefIterator<std::vector<NumaNode *>::iterator> iterator;

  struct bitmask *all_mask;

  void collect(NumaHWConfig *numa_option = NULL);
  void collect_dram_nodes_meminfo(void);
  void check_dram_nodes_watermark(int watermark_percent);
  int get_node_lowest_cpu(int node);

  int nr_node(void)
  {
    return max_node + 1;
  }

  NumaNode *get_node(int nid)
  {
    return nodes[nid];
  }

  NumaNode& operator[](int nid)
  {
    return *get_node(nid);
  }

  iterator begin(void)
  {
    iterator it(nodes.begin(), nodes.end());
    return it.skip_null();
  }

  iterator end(void)
  {
    iterator it(nodes.end(), nodes.end());
    return it;
  }

  iterator dram_begin(void)
  {
    iterator it(dram_nodes.begin(), dram_nodes.end());
    return it.skip_null();
  }

  iterator dram_end(void)
  {
    iterator it(dram_nodes.end(), dram_nodes.end());
    return it;
  }

  iterator pmem_begin(void)
  {
    iterator it(pmem_nodes.begin(), pmem_nodes.end());
    return it.skip_null();
  }

  iterator pmem_end(void)
  {
    iterator it(pmem_nodes.end(), pmem_nodes.end());
    return it;
  }

  NumaNode *node_of_cpu(int cpu)
  {
    return get_node(cpu_node_map[cpu]);
  }

  bool is_valid_nid(int nid)
  {
    return nid >= 0 && nid <= max_node && nodes[nid];
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
