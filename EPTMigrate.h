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

struct MigrateStats: public MoveStats
{
    unsigned long anon_kb;

    void clear();
    void show(Formatter& fmt, MigrateWhat mwhat);
};

class EPTMigrate : public EPTScan
{
  public:
    EPTMigrate();

    int migrate();
    int migrate(ProcIdlePageType type);
    void set_throttler(BandwidthLimit* new_throttler)
    { migrator.set_throttler(new_throttler); }

 private:
    size_t get_threshold_refs(ProcIdlePageType type, int& min_refs, int& max_refs);

    // select max counted pages in page_refs_4k and page_refs_2m
    int select_top_pages(ProcIdlePageType type);

    long do_move_pages(ProcIdlePageType type);

    // status => count
    std::unordered_map<int, int> calc_migrate_stats();

  private:
    // The Virtual Address of hot/cold pages.
    // [0...n] = [VA0...VAn]
    //std::vector<unsigned long> hot_pages;
    std::vector<void *> pages_addr[PMD_ACCESSED + 1];

    std::vector<int> migrate_target_node;

    // Get the status after migration
    std::vector<int> migrate_status;

    MigrateStats migrate_stats;
    MovePages migrator;

    Formatter fmt;
};

#endif
// vim:set ts=2 sw=2 et:
