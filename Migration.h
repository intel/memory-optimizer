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
  MIGRATE_HOT_PAGES = 1,      // migrate hot pages
  MIGRATE_COLD_PAGES = 2,     // migrate cold pages
  MIGRATE_HOT_COLD_PAGES = 3, // migrate hot and cold pages
}MIGRATE_TYPE;

struct migrate_policy {
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
                MIGRATE_TYPE type);

    // set samples and pages percent for policy
    int set_policy(int samples_percent, int pages_percent,
                   int node, bool hot);

  private:
    // functions

	// walk the page_refs_4k and page_refs_2m
    int walk(std::unordered_map<unsigned long, unsigned char>& page_refs);

    // get the numa node in which the pages are
    int get_pages(std::vector<void *>& pages,
                  std::vector<int>& nodes,
                  bool hot);

  private:
    // variables
    static const int DRAM_NUMA_NODE = 0;
    static const int PMEM_NUMA_NODE = 1;
    static const int DEFAULT_HOT_PERCENT = 20;
    static const int DEFAULT_COLD_PERCENT = 30;

    // which type of pages should be migrated
    MIGRATE_TYPE type;

    // the policy for hot pages ?
    struct migrate_policy hot_policy;
    // the policy for cold pages ?
    struct migrate_policy cold_policy;

    // The nodes in which the hot pages are.
    // [0...n] = [TO_NODE0...TO_NODEn]
    std::vector<int> hot_nodes;

    // The nodes in which the cold pages are.
    // [0...n] = [TO_NODE0...TO_NODEn]
    std::vector<int> cold_nodes;

    // The Virtual Address of the hot pages.
    // [0...n] = [VA0...VAn]
    //std::vector<unsigned long> hot_pages;
    std::vector<void *> hot_pages;

    // The Virtual Address of the cold pages.
    // [0...n] = [VA0...VAn]
    //std::vector<unsigned long> cold_pages;
    std::vector<void *> cold_pages;

    // Get the status after migration
    std::vector<int> migrate_status;
};

#endif
