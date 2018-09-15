#include <stdio.h>
#include <map>
#include <string>
#include <iostream>
#include <algorithm>
#include <sys/mman.h>

#include <numa.h>
#include <numaif.h>
#include "Migration.h"
#include "lib/debug.h"
#include "lib/stats.h"

using namespace std;

Migration::Migration(ProcIdlePages& pip)
  : proc_idle_pages(pip)
{
  migrate_target_node.resize(PMD_ACCESSED + 1);
  migrate_target_node[PTE_IDLE]      = PMEM_NUMA_NODE;
  migrate_target_node[PTE_ACCESSED]  = DRAM_NUMA_NODE;

  migrate_target_node[PMD_IDLE]      = PMEM_NUMA_NODE;
  migrate_target_node[PMD_ACCESSED]  = DRAM_NUMA_NODE;
}

int Migration::select_top_pages(ProcIdlePageType type)
{
  const page_refs_map& page_refs = proc_idle_pages.get_pagetype_refs(type).page_refs;
  vector<unsigned long> refs_count = proc_idle_pages.get_pagetype_refs(type).refs_count;
  int nr_walks = proc_idle_pages.get_nr_walks();

  if (page_refs.empty())
    return 1;

  // XXX: this assumes all processes have same hot/cold distribution
  long portion = ((double) page_refs.size() *
                  proc_vmstat.anon_capacity(migrate_target_node[type]) /
                  proc_vmstat.anon_capacity());

  if (type & PAGE_ACCESSED_MASK) {
    int min_refs = nr_walks;
    for (; min_refs > nr_walks / 2; min_refs--) {
      portion -= refs_count[min_refs];
      if (portion <= 0)
        break;
    }

    for (auto it = page_refs.begin(); it != page_refs.end(); ++it) {
      printdd("vpfn: %lx count: %d\n", it->first, (int)it->second);
      if (it->second >= min_refs)
        pages_addr[type].push_back((void *)(it->first << PAGE_SHIFT));
    }
  } else {
    int max_refs = 0;
    for (; max_refs <= nr_walks / 2; max_refs++) {
      portion -= refs_count[max_refs];
      if (portion <= 0)
        break;
    }

    for (auto it = page_refs.begin(); it != page_refs.end(); ++it) {
      printdd("vpfn: %lx count: %d\n", it->first, (int)it->second);
      if (it->second <= max_refs)
        pages_addr[type].push_back((void *)(it->first << PAGE_SHIFT));
    }
  }

  if (pages_addr[type].empty())
    return 1;

  sort(pages_addr[type].begin(), pages_addr[type].end());

  if (debug_level() >= 2)
    for (size_t i = 0; i < pages_addr[type].size(); ++i) {
      cout << "page " << i << ": " << pages_addr[type][i] << endl;
    }

  return 0;
}

int Migration::locate_numa_pages(ProcIdlePageType type)
{
  pid_t pid = proc_idle_pages.get_pid();
  vector<void *>::iterator it;
  int ret;

  auto& addrs = pages_addr[type];

  int nr_pages = addrs.size();

  if (!nr_pages)
    return 1;

  migrate_status.resize(nr_pages);
  ret = move_pages(pid, nr_pages, &addrs[0], NULL,
                   &migrate_status[0], MPOL_MF_MOVE);
  if (ret) {
    perror("locate_numa_pages: move_pages");
    return ret;
  }

  int i, j;
  for (i = 0, j = 0; i < nr_pages; ++i) {
    if (migrate_status[i] >= 0 &&
        migrate_status[i] != migrate_target_node[type])
      addrs[j++] = addrs[i];
  }

  show_migrate_stats(type, "before migrate");

  addrs.resize(j);

  return 0;
}

int Migration::migrate(ProcIdlePageType type)
{
  pid_t pid = proc_idle_pages.get_pid();
  std::vector<int> nodes;
  int ret;

  ret = select_top_pages(type);
  if (ret)
    return ret;

  ret = locate_numa_pages(type);
  if (ret)
    return ret;

  auto& addrs = pages_addr[type];

  int nr_pages = addrs.size();

  migrate_status.resize(nr_pages);
  nodes.resize(nr_pages, migrate_target_node[type]);
  if (!nr_pages)
    return 1;

  ret = move_pages(pid, nr_pages, &addrs[0], &nodes[0],
                   &migrate_status[0], MPOL_MF_MOVE);
  if (ret) {
    perror("migrate: move_pages");
    return ret;
  }

  show_migrate_stats(type, "after migrate");

  return ret;
}

std::unordered_map<int, int> Migration::calc_migrate_stats()
{
  std::unordered_map<int, int> stats;

  for(int &i : migrate_status)
    inc_count(stats, i);

  return stats;
}

void Migration::show_migrate_stats(ProcIdlePageType type, const char stage[])
{
    unsigned long total_kb = proc_idle_pages.get_pagetype_refs(type).page_refs.size() * (pagetype_size[type] >> 10);
    unsigned long to_migrate = pages_addr[type].size() * (pagetype_size[type] >> 10);

    printf("    %s: %s\n", pagetype_name[type], stage);

    printf("%'15lu       TOTAL\n", total_kb);
    printf("%'15lu  %2d%%  TO_migrate\n", to_migrate, percent(to_migrate, total_kb));

    auto stats = calc_migrate_stats();
    for(auto &kv : stats)
    {
      int status = kv.first;
      unsigned long kb = kv.second * (pagetype_size[type] >> 10);

      if (status >= 0)
        printf("%'15lu  %2d%%  IN_node %d\n", kb, percent(kb, total_kb), status);
      else
        printf("%'15lu  %2d%%  %s\n", kb, percent(kb, total_kb), strerror(-status));
    }
}
