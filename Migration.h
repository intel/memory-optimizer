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
  MIGRATE_NONE,
  MIGRATE_HOT,
  MIGRATE_COLD,
  MIGRATE_BOTH = MIGRATE_HOT | MIGRATE_COLD,
} MigrateWhat;

class Migration
{
  public:
    // functions
    Migration(ProcIdlePages& pip);
    ~Migration() {};

    static MigrateWhat parse_migrate_name(std::string name);

    // migrate pages to nodes
    int migrate(ProcIdlePageType type);

 private:
    // functions

    void get_threshold_refs(ProcIdlePageType type, int& min_refs, int& max_refs);

    // select max counted pages in page_refs_4k and page_refs_2m
    int select_top_pages(ProcIdlePageType type);

    // get the numa node in which the pages are
    int locate_numa_pages(ProcIdlePageType type);

    long do_move_pages(ProcIdlePageType type, const int *nodes);

    // status => count
    std::unordered_map<int, int> calc_migrate_stats();

    void show_migrate_stats(ProcIdlePageType type, const char stage[]);

  private:
    // variables
    static const int DRAM_NUMA_NODE = 0;
    static const int PMEM_NUMA_NODE = 1;

    static std::unordered_map<std::string, MigrateWhat> migrate_name_map;

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
