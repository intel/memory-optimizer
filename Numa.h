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

#include "common.h"
#include "Option.h"

/*
 * BaseIterator is iterator for container with element type "T *", so
 * acts like "T **".  And container elements may be NULL.
 * DerefIterator will skip NULL elements automatically and acts like
 * "T *".
 */
template<class BaseIterator, class T>
class DerefIterator : public std::iterator<std::input_iterator_tag, T> {
  BaseIterator it_curr;
  BaseIterator it_end;

public:
  DerefIterator(const DerefIterator<BaseIterator, T>& ait) :
    it_curr(ait.it_curr), it_end(ait.it_end) {}
  DerefIterator(const BaseIterator& curr, const BaseIterator& end) :
    it_curr(curr), it_end(end) {}
  DerefIterator(void) {}
  bool operator==(const DerefIterator<BaseIterator, T>& ait)
  {
    return it_curr == ait.it_curr;
  }
  bool operator!=(const DerefIterator<BaseIterator, T>& ait)
  {
    return it_curr != ait.it_curr;
  }
  DerefIterator<BaseIterator, T>& skip_null(void)
  {
    while (it_curr != it_end && !*it_curr)
      it_curr++;
    return *this;
  }
  DerefIterator<BaseIterator, T>& operator++(void)
  {
    it_curr++;
    return skip_null();
  }
  DerefIterator<BaseIterator, T>& operator++(int)
  {
    it_curr++;
    return skip_null();
  }
  T& operator*() { return **it_curr; }
  T *operator->() { return *it_curr; }
};

enum numa_node_type {
  NUMA_NODE_DRAM,
  NUMA_NODE_PMEM,
};

class NumaNode {
  int id_;
  enum numa_node_type type_;
  long mem_total;
  long mem_free;

public:
  bool mem_watermark_ok;
  NumaNode *promote_target;

  NumaNode(int nid, enum numa_node_type node_type) :
    id_(nid), type_(node_type) {}
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
  typedef DerefIterator<std::vector<NumaNode *>::iterator, NumaNode> iterator;

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

private:
    void collect_by_config(NumaHWConfig *numa_option);
    void collect_by_sysfs(void);
    int parse_sysfs_per_node(int node_id);
    int parse_field(const char* field_name, std::string &value);
};

#endif /* __NUMA__HH__ */
// vim:set ts=2 sw=2 et:
