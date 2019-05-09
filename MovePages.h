/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

#ifndef _MOVE_PAGES_H
#define _MOVE_PAGES_H

#include <unordered_map>
#include <string>
#include <vector>

#include "ProcIdlePages.h"

class BandwidthLimit;
class Formatter;
class NumaNodeCollection;
class NumaNode;

#define MPOL_MF_SW_YOUNG (1<<7)

typedef std::unordered_map<int, unsigned long> MovePagesStatusCount;
class NumaNodeCollection;
class PidContext;

struct MoveStats
{
    unsigned long to_move_kb;
    unsigned long skip_kb;
    unsigned long move_kb;

    const unsigned int from_shift = 0;
    const unsigned int to_shift = 8;
    const unsigned int result_shift = 16;
    MovePagesStatusCount move_page_status;

    MoveStats() { clear(); }
    void clear();
    void add(MoveStats *s);
    void account(MovePagesStatusCount& status_count, int page_shift, int target_node);
    void save_move_states(int status,
                          int target_nodes,
                          int status_after_move,
                          unsigned long page_shift);
    void show_move_state(Formatter& fmt);
    unsigned long get_moved_bytes();

    void save_migrate_states(unsigned long page_shift,
                            std::vector<int>& from_nid,
                            std::vector<int>& target_nid,
                            std::vector<int>& migrate_result);

    static int default_failed;
    static bool is_page_moved(int from, int to, int move_state)
    { return from >= 0 && from != to && to == move_state; }

    static bool is_page_move_failed(int from, int to, int move_state)
    { return from >= 0 && from != to && move_state < 0; }
  private:
    int box_movestate(int status, int target_node, int result);
   void unbox_movestate(int key,
                        int& status, int& target_node, int& result);
};

class MovePages
{
  public:
    unsigned long dram_kb;
    unsigned long pmem_kb;

    MovePages();
    ~MovePages() {};

    void set_pid(pid_t i)                     { pid = i; }
    void set_page_shift(int t)                { page_shift = t; }
    void set_batch_size(unsigned long npages) { batch_size = npages; }
    void set_flags(int f)                     { flags = f; }
    void set_throttler(BandwidthLimit* new_throttler) { throttler = new_throttler; }
    void set_migration_type(ProcIdlePageType new_type) { type = new_type; }
    long move_pages(std::vector<void *>& addrs, bool is_locate);
    long move_pages(void **addrs, std::vector<int> &move_status,
                    unsigned long count, bool is_locate);
    long move_pages(void **addrs, int* target_nid, unsigned long size);
    long locate_move_pages(PidContext* pid_context, std::vector<void *>& addrs,
                           MoveStats *stats);
    void set_numacollection(NumaNodeCollection* new_collection)
    { numa_collection = new_collection; }

    std::vector<int>& get_status()            { return status; }
    MovePagesStatusCount& get_status_count()  { return status_count; }
    std::vector<int>& get_migration_result()  { return status_after_move; }

    void clear_status_count()                 { status_count.clear(); }
    void calc_status_count();
    void add_status_count_to(MovePagesStatusCount& status_sum);
    void show_status_count(Formatter* fmt, MovePagesStatusCount& status_sum);
    void account_stats_count(MoveStats *stats);
    void calc_target_nodes(void **addrs, long size);
    int  get_target_node(NumaNode* node_obj);
    bool is_node_in_target_set(int node_id);
    long find_last_good(std::vector<int>& status, long end_pos);
    void dump_target_nodes(void);
    void calc_memory_state(MovePagesStatusCount& status_sum,
                           unsigned long &total_kb,
                           unsigned long &total_dram_kb,
                           unsigned long &total_pmem_kb);
  private:
    bool is_exceed_dram_quota(PidContext* pid_context);
    void dec_dram_quota(PidContext* pid_context, long dec_value);

    long calc_and_save_state(MoveStats* stats,
                             std::vector<int>& status,
                             std::vector<int>& target_nodes,
                             std::vector<int>& status_after_move);
  private:
    pid_t pid;
    int flags;

    unsigned long page_shift;
    unsigned long batch_size; // used by locate_move_pages()

    // Get the status after migration
    std::vector<int> status;
    std::vector<int> status_after_move;
    std::vector<int> target_nodes;

    MovePagesStatusCount status_count;

    BandwidthLimit* throttler;

    ProcIdlePageType type;

    NumaNodeCollection* numa_collection;
};

#endif
// vim:set ts=2 sw=2 et:
