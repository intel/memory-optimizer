/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Yao Yuan <yuan.yao@intel.com>
 *          Fengguang Wu <fengguang.wu@intel.com>
 */

#ifndef _MIGRATION_H
#define _MIGRATION_H

#include <sys/types.h>

#include <unordered_map>
#include <string>
#include <vector>

#include "Option.h"
#include "Formatter.h"
#include "MovePages.h"
#include "ProcVmstat.h"
#include "ProcIdlePages.h"
#include "EPTScan.h"
#include "PidContext.h"

class BandwidthLimit;
class NumaNodeCollection;
class ProcVmstat;

enum {
    COLD_MIGRATE = 0,
    HOT_MIGRATE,
    MAX_MIGRATE
};

struct MigrateResult {
    long hot_threshold;
    long cold_threshold;
};

struct MigrateStats: public MoveStats
{
    unsigned long anon_kb;

    void clear();
    void add(MigrateStats *s);
    void show(Formatter& fmt, int mwhat);
    void show_move_result_state(Formatter& fmt);
};

class EPTMigrate : public EPTScan
{
  public:
    int dram_percent;

    EPTMigrate();

    int migrate();
    int migrate(ProcIdlePageType type);

    void set_throttler(BandwidthLimit* new_throttler)
    {
      throttler = new_throttler;
      migrator.set_throttler(new_throttler);
    }

    void set_pid_context(PidContext *new_context)
    { context = new_context; }

    static void reset_sys_migrate_stats();
    void count_migrate_stats();

    MigrateStats& get_migrate_stats() {
      return migrate_stats;
    }

    void set_migrate_nr_promote(ProcIdlePageType type, long new_nr) {
      nr_migrate_promote[type] = new_nr;
    }

    void set_migrate_nr_demote(ProcIdlePageType type, long new_nr) {
      nr_migrate_demote[type] = new_nr;
    }

    const MigrateResult& get_migrate_result() {
      return migrate_result;
    }
 private:
    size_t get_threshold_refs(ProcIdlePageType type, int& min_refs, int& max_refs);

    // select max counted pages in page_refs_4k and page_refs_2m
    int select_top_pages(ProcIdlePageType type);

    long do_move_pages(ProcIdlePageType type);

    // status => count
    std::unordered_map<int, int> calc_migrate_stats();

    unsigned long calc_numa_anon_capacity(ProcIdlePageType type, ProcVmstat& proc_vmstat);

    int promote_and_demote(ProcIdlePageType type, long nr_promte, long nr_demote);

    int save_migrate_parameter(void* addr, int nid,
                               std::vector<void*>& addr_array,
                               std::vector<int>& from_nid_array,
                               std::vector<int>& target_nid_array);

    int do_interleave_move_pages(ProcIdlePageType type,
                                 std::vector<void*> *addr,
                                 std::vector<int> *from_nid,
                                 std::vector<int> *target_nid);

    void setup_migrator(ProcIdlePageType type, MovePages& migrator);

    void update_migrate_state(int migrate_type);

  public:
    static MigrateStats sys_migrate_stats;

  private:
    // The Virtual Address of hot/cold pages.
    // [0...n] = [VA0...VAn]
    //std::vector<unsigned long> hot_pages;
    std::vector<void *> pages_addr[PMD_ACCESSED + 1];

    // Get the status after migration
    std::vector<int> migrate_status;

    MigrateStats migrate_stats;
    MovePages migrator;

    Formatter fmt;
    PidContext *context = NULL;

    MigrateStats page_migrate_stats[MAX_MIGRATE];
    MovePages page_migrator[MAX_MIGRATE];

    BandwidthLimit* throttler = NULL;

    long nr_migrate_promote[MAX_ACCESSED];
    long nr_migrate_demote[MAX_ACCESSED];

    struct MigrateResult migrate_result;
};

#endif
// vim:set ts=2 sw=2 et:
