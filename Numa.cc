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

#include "Common.h"
#include "hmd-config.h"
#include "Numa.h"

void NumaNodeCollection::init_cpu_map(void)
{
  int cpu, nr_cpu;
  iterator node;
  struct bitmask *cpumask = numa_allocate_cpumask();

  if (!cpumask)
    err("Allocate cpumask");
  nr_cpu = numa_num_possible_cpus();
  nr_cpu = nr_cpu;
  cpu_node_map.resize(nr_cpu);
  for (node = begin(); node != end(); node++) {
    numa_bitmask_clearall(cpumask);
    if (numa_node_to_cpus(node->id(), cpumask) < 0)
      err("numa_node_to_cpus");
    for (cpu = 0; cpu < nr_cpu; cpu++) {
      if (numa_bitmask_isbitset(cpumask, cpu))
        cpu_node_map[cpu] = node->id();
    }
  }
  numa_free_cpumask(cpumask);
}

void NumaNodeCollection::collect(void)
{
  int i, pmem_node, dram_node;
  struct bitmask *dram_mask, *pmem_mask;
  iterator node;
  const char *p;

  /*
   * FIXME: Rewrite NUMA topology parsing with HMAT utility
   * after it is available.
   */
  max_node = numa_max_node();
  dram_mask = numa_parse_nodestring(hmd_config.numa_dram_mask);
  pmem_mask = numa_parse_nodestring(hmd_config.numa_pmem_mask);

  all_mask = numa_allocate_nodemask();
  numa_bitmask_clearall(all_mask);
  for (i = 0; i <= max_node; i++) {
    if (numa_bitmask_isbitset(dram_mask, i) ||
        numa_bitmask_isbitset(pmem_mask, i))
      numa_bitmask_setbit(all_mask, i);
  }

  nodes.resize(max_node + 1);
  for (i = 0; i <= max_node; i++) {
    NumaNode *pnode = NULL;

    if (numa_bitmask_isbitset(dram_mask, i)) {
      pnode = new NumaNode(i, NUMA_NODE_DRAM);
      dram_nodes.push_back(pnode);
    } else if (numa_bitmask_isbitset(pmem_mask, i)) {
      pnode = new NumaNode(i, NUMA_NODE_PMEM);
      pmem_nodes.push_back(pnode);
    }
    nodes[i] = pnode;
  }

  /* node maps to itself by default */
  for (node = dram_begin(); node != dram_end(); node++)
    node->promote_target = &*node;

  p = hmd_config.pmem_dram_map - 1;
  do {
    p++;
    if (sscanf(p, "%d->%d", &pmem_node, &dram_node) != 2 ||
        pmem_node > max_node ||
        dram_node > max_node) {
      fprintf(stderr, "Invalid pmem to dram map: %s\n",
        hmd_config.pmem_dram_map);
      exit(1);
    }
    get_node(pmem_node)->promote_target = get_node(dram_node);
  } while ((p = strstr(p, ",")) != NULL);

  init_cpu_map();

  numa_free_nodemask(dram_mask);
  numa_free_nodemask(pmem_mask);
}

void NumaNodeCollection::collect_dram_nodes_meminfo(void)
{
  for (iterator node = dram_begin(); node != dram_end(); node++)
    node->collect_meminfo();
}

void NumaNodeCollection::check_dram_nodes_watermark(int watermark_percent)
{
  for (iterator node = dram_begin(); node != dram_end(); node++)
    node->check_watermark(watermark_percent);
}

int NumaNodeCollection::get_node_lowest_cpu(int node)
{
  for (int cpu = 0; cpu < nr_cpu; cpu++) {
    if (cpu_node_map[cpu] == node)
      return cpu;
  }

  return -1;
}
