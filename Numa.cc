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

#include <stdio.h>
#include <string.h>
#include <linux/limits.h>

#include <map>

#include "common.h"
#include "Numa.h"


static std::unordered_map<std::string, numa_node_type> numa_type_map = {
  {"ram", NUMA_NODE_DRAM}, // keep for a while to work with legacy kernel
  {"dram", NUMA_NODE_DRAM},
  {"pmem", NUMA_NODE_PMEM},
};

static const char* str_type      = "type";
static const char* str_peer_node = "peer_node";


void NumaNodeCollection::init_cpu_map(void)
{
  int cpu;
  struct bitmask *cpumask = numa_allocate_cpumask();

  if (!cpumask)
    sys_err("Allocate cpumask");
  nr_possible_cpu_ = numa_num_possible_cpus();
  cpu_node_map.resize(nr_possible_cpu_);
  for (auto& node: nodes) {
    int nid = node->id();
    numa_bitmask_clearall(cpumask);
    if (numa_node_to_cpus(nid, cpumask) < 0)
      sys_err("numa_node_to_cpus");
    for (cpu = 0; cpu < nr_possible_cpu_; cpu++) {
      if (numa_bitmask_isbitset(cpumask, cpu))
        cpu_node_map[cpu] = nid;
    }
  }
  numa_free_cpumask(cpumask);
}

void NumaNodeCollection::collect(NumaHWConfig *numa_option)
{
  nr_possible_node_ = numa_max_node() + 1;
  // numa_option has higher priority
  // FIXME: need sync collect_by_config() with new added fields (demote_node and so on)
  //        before enable overwrite logic
  if (numa_option && numa_option->is_valid())
    collect_by_config(numa_option);
  else
    collect_by_sysfs();

  dump();

}

void NumaNodeCollection::collect_by_config(NumaHWConfig *numa_option)
{
  int i, from, to;
  struct bitmask *dram_mask, *pmem_mask;
  iterator node;
  const char *p;

  /*
   * FIXME: Rewrite NUMA topology parsing with HMAT utility
   * after it is available.
   */
  dram_mask = numa_parse_nodestring(numa_option->numa_dram_list.c_str());
  pmem_mask = numa_parse_nodestring(numa_option->numa_pmem_list.c_str());
  if (!dram_mask || !pmem_mask) {
    fprintf(stderr, "Invalid nodemask: dram_mask=%p(%s), pmem_mask=%p(%s)\n",
            dram_mask, numa_option->numa_dram_list.c_str(),
            pmem_mask, numa_option->numa_pmem_list.c_str());
    exit(1);
  }

  node_map.resize(nr_possible_node_);
  for (i = 0; i < nr_possible_node_; i++) {
    NumaNode *pnode = NULL;

    if (numa_bitmask_isbitset(dram_mask, i)) {
      pnode = new NumaNode(i, NUMA_NODE_DRAM);
      dram_nodes.push_back(pnode);
    } else if (numa_bitmask_isbitset(pmem_mask, i)) {
      pnode = new NumaNode(i, NUMA_NODE_PMEM);
      pmem_nodes.push_back(pnode);
    } else
      continue;

    nodes.push_back(pnode);
    node_map[i] = pnode;
  }

  /* node maps to itself by default */
  set_default_target_node();

  p = numa_option->pmem_dram_map.c_str() - 1;
  do {
    p++;
    if (sscanf(p, "%d->%d", &from, &to) != 2 ||
        from >= nr_possible_node_ ||
        to >= nr_possible_node_) {
      fprintf(stderr, "Invalid pmem to dram map: %s\n",
              numa_option->pmem_dram_map.c_str());
      exit(1);
    }
    //get_node(pmem_node)->promote_target = get_node(dram_node);
    get_node(from)->set_peer_node(get_node(to));
  } while ((p = strstr(p, ",")) != NULL);

  init_cpu_map();

  numa_free_nodemask(dram_mask);
  numa_free_nodemask(pmem_mask);
}

void NumaNodeCollection::collect_by_sysfs(void)
{
  int err;
  NumaInfo numa_info;

  err = load_numa_info(numa_info, nr_possible_node_);
  if (err < 0) {
    fprintf(stderr, "failed to load_numa_info(), err = %d\n", err);
    return;
  }

  node_map.resize(nr_possible_node_);
  err = create_node_objects(numa_info);
  if (err < 0) {
    fprintf(stderr, "failed to create_node_objects, err = %d\n", err);
    return;
  }

  set_default_target_node();
  setup_node_relationship(numa_info, false);

  init_cpu_map();
}

int NumaNodeCollection::parse_sysfs_per_node(int node_id,
                                             NodeInfo& node_info)
{
  const char *node_path = "/sys/devices/system/node/node%d/%s";
  char path[PATH_MAX];
  int err;

  node_info[str_type] = "";
  node_info[str_peer_node] = "";

  for (auto &i : node_info) {
    snprintf(path, sizeof(path),
             node_path,
             node_id, i.first);
    err = parse_field(path, i.second);
    if (err < 0)
      return err;
  }

  return 0;
}

int NumaNodeCollection::parse_field(const char *field_name,
                                    std::string &value)
{
  FILE *file;
  char read_buf[32];

  file = fopen(field_name, "r");
  if (!file) {
    fprintf(stderr, "open %s failed, err = %d\n",
            field_name, -errno);
    return -errno;
  }

  do {

    if (!fgets(read_buf, sizeof(read_buf), file)) {
      fprintf(stderr, "read %s failed\n",
              field_name);
      break;
    }

    value = read_buf;
    value.pop_back(); // trim trailing '\n'

  }while(0);

  fclose(file);
  return 0;
}

numa_node_type NumaNodeCollection::get_numa_type(std::string &type_str)
{
  auto iter = numa_type_map.find(type_str);

  if (iter == numa_type_map.end())
    return NUMA_NODE_END;

  return iter->second;
}

int NumaNodeCollection::create_node(int node_id, numa_node_type type)
{
  if(type >= NUMA_NODE_END)
    return -2;

  NumaNode *new_node;
  new_node = new NumaNode(node_id, type);

  switch(type) {
    case NUMA_NODE_DRAM:
      dram_nodes.push_back(new_node);
      break;
    case NUMA_NODE_PMEM:
      pmem_nodes.push_back(new_node);
      break;
    default:
      break;
  }

  nodes.push_back(new_node);
  node_map[node_id] = new_node;

  return 0;
}

void NumaNodeCollection::set_default_target_node()
{
  /* node maps to itself by default */
  for (auto& node: dram_nodes)
    node->promote_target = node;

  for (auto& node: pmem_nodes)
    node->demote_target = node;
}

void NumaNodeCollection::setup_node_relationship(NumaInfo& numa_info, bool is_bidir)
{
  int peer_node_id;

  for (size_t i = 0; i < numa_info.size(); ++i) {
    peer_node_id = atoi(numa_info[i][str_peer_node].c_str());
    set_target_node(i, peer_node_id, is_bidir);
  }
}

void NumaNodeCollection::set_target_node(int node_id, int target_node_id, bool is_bidir)
{
  if (node_id >= nr_possible_node_
      || target_node_id >= nr_possible_node_
      || target_node_id < 0) {
    fprintf(stderr, "wrong node id: node_id = %d target_node_id = %d max id = %d\n",
            node_id, target_node_id, nr_possible_node_ - 1);
    return;
  }

  node_map[node_id]->set_peer_node(node_map[target_node_id]);
  if (is_bidir)
    node_map[target_node_id]->set_peer_node(node_map[node_id]);
}

int NumaNodeCollection::load_numa_info(NumaInfo& numa_info, int node_count)
{
  int err;
  NodeInfo node_info;

  numa_info.resize(node_count);

  for (int i = 0; i < node_count; ++i) {
    node_info.clear();
    err = parse_sysfs_per_node(i, node_info);
    if (err >= 0)
      numa_info[i] = node_info;
  }

  return 0;
}

int NumaNodeCollection::create_node_objects(NumaInfo& numa_info)
{
  numa_node_type node_type;
  int err;

  for (size_t i = 0; i < numa_info.size(); ++i) {
    node_type = get_numa_type(numa_info[i][str_type]);
    err = create_node(i, node_type);
    if (err < 0)
      return err;
  }

  return 0;
}

void NumaNodeCollection::collect_dram_nodes_meminfo(void)
{
  for (auto& node: dram_nodes)
    node->collect_meminfo();
}

void NumaNodeCollection::check_dram_nodes_watermark(int watermark_percent)
{
  for (auto& node: dram_nodes)
    node->check_watermark(watermark_percent);
}

int NumaNodeCollection::get_node_lowest_cpu(int node)
{
  for (int cpu = 0; cpu < nr_possible_cpu_; cpu++) {
    if (cpu_node_map[cpu] == node)
      return cpu;
  }

  return -1;
}

void NumaNodeCollection::dump()
{
  NumaNode* peer_node;

  printf("All nodes:\n");
  for (auto& numa_obj : nodes) {

    peer_node = numa_obj->get_peer_node();
    if (peer_node)
      printf("Node %d type:%d peer_id:%d peer_type:%d\n",
             numa_obj->id(),
             numa_obj->type(),
             peer_node->id(),
             peer_node->type());
    else
      printf("Node %d type:%d no peer node\n",
             numa_obj->id(),
             numa_obj->type());
  }

  printf("DRAM nodes:\n");
  for (auto& numa_obj : dram_nodes)
    printf("Node %d type:%d\n",
           numa_obj->id(),
           numa_obj->type());

  printf("PMEM nodes:\n");
  for (auto& numa_obj : pmem_nodes)
    printf("Node %d type:%d\n",
           numa_obj->id(),
           numa_obj->type());
}
