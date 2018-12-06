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

class BandwidthLimit;
class NumaNodeCollection;
class ProcVmstat;

struct MigrateStats: public MoveStats
{
    unsigned long anon_kb;

    void clear();
    void add(MigrateStats *s);
    void show(Formatter& fmt, MigrateWhat mwhat);
    void show_move_result_state(Formatter& fmt);
};

class EPTMigrate : public EPTScan
{
  public:
    EPTMigrate();

    int migrate();
    int migrate(ProcIdlePageType type);

    void set_throttler(BandwidthLimit* new_throttler)
    { migrator.set_throttler(new_throttler); }

    void set_numacollection(NumaNodeCollection* new_numa_collection) {
      numa_collection = new_numa_collection;
      migrator.set_numacollection(new_numa_collection);
    }

    static void reset_sys_migrate_stats();
    void count_migrate_stats();

 private:
    size_t get_threshold_refs(ProcIdlePageType type, int& min_refs, int& max_refs);

    // select max counted pages in page_refs_4k and page_refs_2m
    int select_top_pages(ProcIdlePageType type);

    long do_move_pages(ProcIdlePageType type);

    // status => count
    std::unordered_map<int, int> calc_migrate_stats();

    unsigned long calc_numa_anon_capacity(ProcIdlePageType type, ProcVmstat& proc_vmstat);

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
    NumaNodeCollection* numa_collection;

    Formatter fmt;
};

#endif
// vim:set ts=2 sw=2 et:
