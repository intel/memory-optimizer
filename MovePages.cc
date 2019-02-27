/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 *          Liu Jingqi <jingqi.liu@intel.com>
 */

#include <numa.h>
#include <numaif.h>
#include <limits.h>
#include <sys/user.h>
#include <utility>

#include "lib/stats.h"
#include "MovePages.h"
#include "Formatter.h"
#include "BandwidthLimit.h"
#include "Numa.h"
#include "PidContext.h"

void MoveStats::clear()
{
  to_move_kb = 0;
  skip_kb = 0;
  move_kb = 0;
  move_page_status.clear();
}

void MoveStats::add(MoveStats* s)
{
  to_move_kb += s->to_move_kb;
  skip_kb += s->skip_kb;
  move_kb += s->move_kb;
}

void MoveStats::save_move_states(std::vector<int>& status,
                                 std::vector<int>& target_nodes,
                                 std::vector<int>& status_after_move,
                                 unsigned long page_shift)
{
  int key;

  for (size_t i = 0; i < status.size(); ++i) {

    // let's what will return if negative value in
    //if (status[i] < 0)
    //  continue;
    key = box_movestate(status[i],
                        target_nodes[i],
                        status_after_move[i]);
    add_count(move_page_status, key, 1 << page_shift);
  }

  return;
}

void MoveStats::show_move_state(Formatter& fmt)
{
  int from, to, result;
  int key;

  for (auto& i : move_page_status) {
    key = i.first;
    unbox_movestate(key, from, to, result);
    fmt.print("from node %d to node %d: %lu KB with result %d\n",
              from, to, i.second >> 10, result);
  }
}

unsigned long MoveStats::get_moved_bytes()
{
  unsigned long moved_bytes = 0;
  int from, to, result;
  int key;

  for (auto& i : move_page_status) {
    key = i.first;
    unbox_movestate(key, from, to, result);

    if (is_page_moved(from, to, result))
      moved_bytes += i.second;
  }

  return moved_bytes;
}

int MoveStats::box_movestate(int status, int target_node, int result)
{
  return (status << from_shift)
         + (target_node << to_shift)
         + (result << result_shift);
}

void MoveStats::unbox_movestate(int key,
                        int& status, int& target_node, int& result)
{
  status = (signed char)((key >> from_shift) & 0xff);
  target_node = (signed char)((key >> to_shift) & 0xff);
  result = (signed char)((key >> result_shift) & 0xff);
}


MovePages::MovePages() :
  flags(MPOL_MF_MOVE | MPOL_MF_SW_YOUNG),
  page_shift(PAGE_SHIFT),
  batch_size(ULONG_MAX),
  throttler(NULL)
{
}

long MovePages::move_pages(std::vector<void *>& addrs, bool is_locate)
{
  return move_pages(&addrs[0], addrs.size(), is_locate);
}

long MovePages::move_pages(void **addrs, unsigned long count, bool is_locate)
{
  std::vector<int> nodes;
  const int *pnodes;
  long ret = 0;

  if (!is_locate) {
    // move pages
    pnodes = &target_nodes[0];
    status_after_move.resize(count);
    ret = ::move_pages(pid, count, addrs, pnodes, &status_after_move[0], flags);
  } else {
    // locate pages
    pnodes = NULL;
    status.resize(count);
    ret = ::move_pages(pid, count, addrs, pnodes, &status[0], flags);
  }

  if (ret < 0)
    perror("WARNING: move_pages");
  else if (ret > 0)
    fprintf(stderr, "WARNING: move_pages failed %ld\n", ret);

  return ret;
}

long MovePages::locate_move_pages(PidContext *pid_context,
                                  std::vector<void *>& addrs,
                                  MoveStats *stats)
{
  unsigned long nr_pages = addrs.size();
  long moved_size = 0;
  long ret = 0;

  for (unsigned long i = 0; i < nr_pages; i += batch_size) {
    unsigned long size = std::min(batch_size, nr_pages - i);

    if (is_exceed_dram_quota(pid_context)) {
      printf("pid %d: skip migration for dram ratio exceed.\n", pid_context->get_pid());
      return 0;
    }

    status.resize(size);

    // locate pages
    ret = move_pages(&addrs[i], size, true);
    if (ret) {
      fprintf(stderr, "WARNING: locate pages failed %ld\n", ret);
      break;
    }

    calc_target_nodes(&addrs[i], size);
    clear_status_count();
    calc_status_count();
    account_stats_count(stats);

    if (debug_level() >= 3)
      dump_target_nodes();

    ret = move_pages(&addrs[i], size, false);
    if (ret >= 0) {
      stats->save_move_states(status, target_nodes,
                              status_after_move, page_shift);
      moved_size = calc_moved_size(status, target_nodes,
                                   status_after_move, page_shift);
      dec_dram_quota(pid_context, moved_size >> 10);
    }
    if (ret) {
      fprintf(stderr, "WARNING: move pages failed %ld\n", ret);
      continue;
    }
  }

  return ret;
}

void MovePages::account_stats_count(MoveStats *stats)
{
  unsigned long skip_kb = 0;
  unsigned long move_kb = 0;
  const int shift = page_shift - 10;

  for (auto &kv : status_count) {
    // Don't calculate invalid pages.
    // The status includes "Bad address or No such file" which return by move_pages.
    // Maybe these pages have no corresponding physical memory.
    if (kv.first < 0)
      continue;
    if (is_node_in_target_set(kv.first))
      skip_kb += kv.second << shift;
    else if (kv.first >= 0)
      move_kb += kv.second << shift;
  }

  stats->to_move_kb += skip_kb + move_kb;
  stats->skip_kb += skip_kb;
  stats->move_kb += move_kb;

  if (throttler)
    throttler->add_and_sleep(move_kb * 1024);
}

void MovePages::calc_status_count()
{
  for (int &i : status)
    inc_count(status_count, i);
}

void MovePages::add_status_count_to(MovePagesStatusCount& status_sum)
{
  for (auto &kv : status_count)
    add_count(status_sum, kv.first, kv.second);
}

void MovePages::show_status_count(Formatter* fmt)
{
  show_status_count(fmt, status_count);
}

void MovePages::show_status_count(Formatter* fmt, MovePagesStatusCount& status_sum)
{
  unsigned long total_kb = 0;
  unsigned long total_dram_kb = 0;
  unsigned long total_pmem_kb = 0;

  calc_memory_state(status_sum,
                    total_kb, total_dram_kb, total_pmem_kb);

  for (auto &kv : status_sum) {
    int nid = kv.first;
    unsigned long kb = kv.second << (page_shift - 10);

    if (nid >= 0)
      fmt->print("%'15lu  %2d%%  node %d\n", kb, percent(kb, total_kb), nid);
    else if (debug_level())
      fmt->print("%'15lu  %2d%%  %s\n", kb, percent(kb, total_kb), strerror(-nid));
  }

  if (numa_collection) {
    fmt->print("Anon DRAM nodes size for pid %d : %'15lu  %2d%%\n",
               pid,
               total_dram_kb,
               percent(total_dram_kb, total_kb));
    fmt->print("Anon PMEM nodes size for pid %d : %'15lu  %2d%%\n",
               pid,
               total_pmem_kb,
               percent(total_pmem_kb, total_kb));
  }
}

void MovePages::calc_memory_state(MovePagesStatusCount &status_sum,
                                  unsigned long &total_kb,
                                  unsigned long &total_dram_kb,
                                  unsigned long &total_pmem_kb)
{
  NumaNode* numa_obj;
  unsigned long kb = 0;
  int nid = 0;

  total_kb = 0;
  total_dram_kb = 0;
  total_pmem_kb = 0;

  for (auto &kv : status_sum) {
    nid = kv.first;
    kb = kv.second << (page_shift - 10);

    // skip invalid pages
    if (!numa_collection || nid < 0)
      continue;
    numa_obj = numa_collection->get_node(nid);
    if (!numa_obj)
      continue;

    total_kb += kb;
    switch(numa_obj->type()) {
      case NUMA_NODE_DRAM:
        total_dram_kb += kb;
        break;
      case NUMA_NODE_PMEM:
        total_pmem_kb += kb;
        break;
      default:
        //do nothing with unknown node type
        break;
    }
  }
}

void MovePages::calc_target_nodes(void **addrs, long size)
{
  NumaNode* numa_obj;
  int last_good_index;
  int i = 0;

  target_nodes.resize(size);

  /*
    in status:
    0, -14, 0, -2, -2 => 0, 0, -14, -2, -2

    also changed the corresponding value in addrs
   */
  while(i < size) {
    if (status[i] >= 0) {
      numa_obj = numa_collection->get_node(status[i]);
      target_nodes[i] = get_target_node(numa_obj);
      ++i;
    } else {
      last_good_index = find_last_good(status, i);
      if (last_good_index == i)
        break;

      std::swap(addrs[i], addrs[last_good_index]);
      std::swap(status[i], status[last_good_index]);
    }
  }
}

int MovePages::get_target_node(NumaNode* node_obj)
{
  NumaNode* peer_node;
  bool is_dram_to_dram, is_pmem_to_pmem;

  if (!node_obj) {
    fprintf(stderr, "get_target_node() node_obj = NULL\n");
    return -1;
  }

  is_dram_to_dram = (type <= MAX_ACCESSED)
                    && !node_obj->is_pmem();
  is_pmem_to_pmem = (!(type <= MAX_ACCESSED))
                    && node_obj->is_pmem();

  if (is_dram_to_dram || is_pmem_to_pmem)
    return node_obj->id();

  peer_node = node_obj->get_peer_node();
  if (peer_node)
    return peer_node->id();

  fprintf(stderr, "get_target_node() failed\n");
  return -2;
}

bool MovePages::is_node_in_target_set(int node_id)
{
  NumaNode* numa_obj;

  if (node_id < 0) {
    fprintf(stderr, "is_node_in_target_set(): unexpected  node_id %d \n",
            node_id);
    return false;
  }

  numa_obj = numa_collection->get_node(node_id);
  if (!numa_obj) {
    fprintf(stderr, "is_node_in_target_set(): non-exist node_id %d \n",
            node_id);
    return false;
  }

  if (type <= MAX_ACCESSED)
    return !numa_obj->is_pmem();
  else
    return numa_obj->is_pmem();
}

long MovePages::find_last_good(std::vector<int>& status, long end_pos)
{
  for (long i = status.size() - 1; i > end_pos; --i) {
    if (status[i] >= 0)
      return i;
  }
  return end_pos;
}

void MovePages::dump_target_nodes(void)
{
  size_t end = std::min(target_nodes.size(), status.size());

  for (size_t i = 0; i < end; ++i) {
    printf("%d -> %d\n", status[i], target_nodes[i]);
  }
}

bool MovePages::is_exceed_dram_quota(PidContext* pid_context)
{
  if (!pid_context)
    return false;
  return pid_context->get_dram_quota() <= 0;
}

void MovePages::dec_dram_quota(PidContext* pid_context, long dec_value)
{
    if (!pid_context)
      return;
    pid_context->sub_dram_quota(dec_value);
}

long MovePages::calc_moved_size(std::vector<int>& status,
                                std::vector<int>& target_nodes,
                                std::vector<int>& status_after_move,
                                unsigned long page_shift)
{
  long moved_bytes = 0;

  for (size_t i = 0; i < status.size(); ++i) {
      if (MoveStats::is_page_moved(status[i],
                                   target_nodes[i],
                                   status_after_move[i]))
          moved_bytes += 1 << page_shift;
  }

  return moved_bytes;
}
