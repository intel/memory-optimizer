#ifndef _MIGRATION_H
#define _MIGRATION_H

/*
 * The header for migrating pages.
 */

#include <sys/types.h>

#include <unordered_map>
#include <string>
#include <vector>

#include "Option.h"
#include "ProcVmstat.h"
#include "ProcIdlePages.h"

class Migration : public ProcIdlePages
{
  public:
    // functions
    Migration(const Option& o);
    ~Migration() {};

    static MigrateWhat parse_migrate_name(std::string name);

    // migrate pages to nodes
    int migrate(ProcIdlePageType type);

    int dump_task_nodes();
    int dump_vma_nodes(proc_maps_entry& vma);

 private:
    // functions

    size_t get_threshold_refs(ProcIdlePageType type, int& min_refs, int& max_refs);

    // select max counted pages in page_refs_4k and page_refs_2m
    int select_top_pages(ProcIdlePageType type);

    // get the numa node in which the pages are
    int locate_numa_pages(ProcIdlePageType type);

    void fill_addrs(std::vector<void *>& addrs, unsigned long start);
    void dump_node_percent();

    long __move_pages(pid_t pid, unsigned long nr_pages,
		      void **addrs, const int *nodes);

    long do_move_pages(ProcIdlePageType type, const int *nodes);

    // status => count
    std::unordered_map<int, int> calc_migrate_stats();

    void show_migrate_stats(ProcIdlePageType type, const char stage[]);
    void show_numa_stats();

  private:
    // variables
    static std::unordered_map<std::string, MigrateWhat> migrate_name_map;

    ProcVmstat proc_vmstat;

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
