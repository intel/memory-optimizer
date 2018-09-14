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
  memset(&policies, 0, sizeof(policies));
}

int Migration::select_top_pages(ProcIdlePageType type)
{
  const page_refs_map& page_refs = proc_idle_pages.get_pagetype_refs(type).page_refs;
  vector<unsigned long> refs_count = proc_idle_pages.get_pagetype_refs(type).refs_count;
  int nr_walks = proc_idle_pages.get_nr_walks();

  unsigned long sum_of_page_refs = page_refs.size();
  // sum of migrate pages: by -g parameter
  unsigned long sum_of_migrate_page = (unsigned long) ((double) sum_of_page_refs *
                                      (double)policies[type].nr_pages_percent / 100.00);
  int nr_count = 0;
  // migrate sample not less than percentage: by -s parameter
  int nr_end = (int)((double)nr_walks * (double)policies[type].nr_samples_percent / 100.00);

  for (nr_count = nr_walks; nr_count > (nr_walks - nr_end + 1); nr_count--) {
    unsigned long tmp_refs = refs_count[nr_count];
    if (sum_of_migrate_page >= tmp_refs) {
      sum_of_migrate_page -= tmp_refs;
    }
    else
      break;
  }

  for (auto it = page_refs.begin(); it != page_refs.end(); ++it) {
    printdd("vpfn: %lx count: %d\n", it->first, (int)it->second);
    if (it->second >= nr_count)
      pages_addr[type].push_back((void *)(it->first << PAGE_SHIFT));
  }

  sort(pages_addr[type].begin(), pages_addr[type].end());

  if (debug_level() >= 2)
    for (size_t i = 0; i < pages_addr[type].size(); ++i) {
      cout << "page " << i << ": " << pages_addr[type][i] << endl;
    }

  return 0;
}

int Migration::set_policy(int samples_percent, int pages_percent,
                          int node, ProcIdlePageType type)
{
  policies[type].nr_samples_percent = samples_percent;
  policies[type].nr_pages_percent = pages_percent;
  policies[type].node = node;

  return 0;
}

int Migration::locate_numa_pages(ProcIdlePageType type)
{
  pid_t pid = proc_idle_pages.get_pid();
  vector<void *>::iterator it;
  int ret;

  auto& params = policies[type];
  auto& addrs = pages_addr[type];

  int nr_pages = addrs.size();
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
        migrate_status[i] != params.node)
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

  auto& params = policies[type];
  auto& addrs = pages_addr[type];

  int nr_pages = addrs.size();

  migrate_status.resize(nr_pages);
  nodes.resize(nr_pages, params.node);
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
    auto stats = calc_migrate_stats();

    printf("%s: %s\n", pagetype_name[type], stage);

    printf("%9lu    Total\n", pages_addr[type].size());

    for(auto &kv : stats)
    {
      int status = kv.first;
      int count = kv.second;

      if (status >= 0)
        printf("%9d    IN_node %d\n", count, status);
      else
        printf("%9d    %s\n", count, strerror(-status));
    }
}
