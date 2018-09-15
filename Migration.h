#ifndef _MIGRATION_H
#define _MIGRATION_H

/*
 * The header for migrating pages.
 */

#include <sys/types.h>

#include <unordered_map>
#include <string>
#include <vector>

#include "ProcVmstat.h"
#include "ProcIdlePages.h"

typedef enum {
  MIGRATE_HOT_PAGES,      // migrate hot pages
  MIGRATE_COLD_PAGES,     // migrate cold pages
  MIGRATE_HOT_COLD_PAGES, // migrate hot and cold pages
} MigrateType;

class Migration
{
  public:
    // functions
    Migration(ProcIdlePages& pip);
    ~Migration() {};

    // migrate pages to nodes
    int migrate(ProcIdlePageType type);

 private:
    // functions

    // select max counted pages in page_refs_4k and page_refs_2m
    int select_top_pages(ProcIdlePageType type);

    // get the numa node in which the pages are
    int locate_numa_pages(ProcIdlePageType type);

    // status => count
    std::unordered_map<int, int> calc_migrate_stats();

    void show_migrate_stats(ProcIdlePageType type, const char stage[]);

  private:
    // variables
    static const int DRAM_NUMA_NODE = 0;
    static const int PMEM_NUMA_NODE = 1;

    ProcVmstat proc_vmstat;
    ProcIdlePages& proc_idle_pages;

    // The Virtual Address of hot/cold pages.
    // [0...n] = [VA0...VAn]
    //std::vector<unsigned long> hot_pages;
    std::vector<void *> pages_addr[PMD_ACCESSED + 1];

    std::vector<int> migrate_target_node;

    // Get the status after migration
    std::vector<int> migrate_status;
};

#endif
// vim:set ts=2 sw=2 et:
