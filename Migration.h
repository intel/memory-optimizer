#ifndef _MIGRATION_H
#define _MIGRATION_H

/*
 * The header for migrating pages.
 */

#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

typedef enum {
  MIGRATE_HOT_PAGES,      // migrate hot pages
  MIGRATE_COLD_PAGES,     // migrate cold pages
  MIGRATE_HOT_COLD_PAGES, // migrate hot and cold pages
} MigrateType;

struct MigratePolicy {
  int nr_samples_percent;
  int nr_pages_percent;
  int node;
};

class Migration
{
  public:
    // functions
    Migration();
    ~Migration() {};

    // migrate pages to nodes
    int migrate(pid_t pid,
                std::unordered_map<unsigned long, unsigned char>& page_refs,
                std::vector<int>& status,
                unsigned long nr_walks,
                MigrateType type);

    // set samples and pages percent for policy
    int set_policy(int samples_percent, int pages_percent, int node, MigrateType type);

  private:
    // functions

    // select max counted pages in page_refs_4k and page_refs_2m
    int select_top_pages(MigrateType type, int max,
			 std::unordered_map<unsigned long, unsigned char>& page_refs);

    // get the numa node in which the pages are
    int locate_numa_pages(MigrateType type);

  private:
    // variables
    static const int DRAM_NUMA_NODE = 0;
    static const int PMEM_NUMA_NODE = 1;
    static const int DEFAULT_HOT_PERCENT = 20;
    static const int DEFAULT_COLD_PERCENT = 30;

    struct MigratePolicy policies[2];

    // The nodes in which the hot/cold pages are.
    // [0...n] = [TO_NODE0...TO_NODEn]
    std::vector<int> pages_node[2];

    // The Virtual Address of hot/cold pages.
    // [0...n] = [VA0...VAn]
    //std::vector<unsigned long> hot_pages;
    std::vector<void *> pages_addr[2];

    // Get the status after migration
    std::vector<int> migrate_status;
};

#endif
