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

using namespace std;

static const char* page_type_to_size[IDLE_PAGE_TYPE_MAX] = {
  [PTE_HOLE]     = "unsupported pages",
  [PTE_IDLE]     = "4KB pages",
  [PTE_ACCESSED] = "4KB pages",

  [PMD_HOLE]     = "unsupported pages",
  [PMD_IDLE]     = "2MB pages",
  [PMD_ACCESSED] = "2MB pages",

  [PUD_HOLE]     = "unsupported pages",
  [PUD_IDLE]     = "1GB pages",
  [PUD_ACCESSED] = "1GB pages",

  [P4D_HOLE]     = "unsupported pages",
  [PGDIR_HOLE]   = "unsupported pages",
};


static const char* page_type_to_migration_type[IDLE_PAGE_TYPE_MAX] = {
  [PTE_HOLE]     = "unsupported type",
  [PTE_IDLE]     = "COLD",
  [PTE_ACCESSED] = "HOT",

  [PMD_HOLE]     = "unsupported type",
  [PMD_IDLE]     = "COLD",
  [PMD_ACCESSED] = "HOT",

  [PUD_HOLE]     = "unsupported type",
  [PUD_IDLE]     = "COLD",
  [PUD_ACCESSED] = "HOT",

  [P4D_HOLE]     = "unsupported type",
  [PGDIR_HOLE]   = "unsupported type",
};


Migration::Migration(ProcIdlePages& pip)
  : proc_idle_pages(pip)
{
  memset(&policies, 0, sizeof(policies));
}

int Migration::select_top_pages(ProcIdlePageType type)
{
  const page_refs_map& page_refs = proc_idle_pages.get_pagetype_refs(type).page_refs;
  int nr_walks = proc_idle_pages.get_nr_walks();
  int nr_pages;

  nr_pages = page_refs.size();
  cout << "nr_pages: " << nr_pages << endl;

  for (auto it = page_refs.begin(); it != page_refs.end(); ++it) {
    printdd("vpfn: %lx count: %d\n", it->first, (int)it->second);

    if (it->second >= nr_walks)
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
    perror("move_pages");
    return ret;
  }

  int i, j;
  for (i = 0, j = 0; i < nr_pages; ++i) {
    if (migrate_status[i] >= 0 &&
        migrate_status[i] != params.node)
      addrs[j++] = addrs[i];
  }

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
  cout << "nr_pages: " << nr_pages << endl;

  migrate_status.resize(nr_pages);
  nodes.resize(nr_pages, params.node);
  ret = move_pages(pid, nr_pages, &addrs[0], &nodes[0],
                   &migrate_status[0], MPOL_MF_MOVE);
  if (ret) {
    perror("move_pages");
    return ret;
  }

  show(type);

  return ret;
}


void Migration::get_migration_result(std::unordered_map<int, int> &result_detail)
{
  for(auto &i : migrate_status)
  {
    auto find_iter = result_detail.find(i);

    if (find_iter == result_detail.end()) {
        result_detail[i] = 1;
    } else {
        result_detail[i] += 1;
    }
  }
}


// show the migration information
void Migration::show(ProcIdlePageType type)
{
  if (pages_addr[type].size() > 0)
  {
    std::unordered_map<int, int> result;

    get_migration_result(result);

    printf("Moving %s %s to node %d:\n",
           page_type_to_migration_type[type],
           page_type_to_size[type],
           policies[type].node);

    printf("Total: %04lu\n",
           pages_addr[type].size());

    for(auto &i : result) {
      printf("  %s(err = %03d): %d\n",
             i.first >= 0 ? "Success":"Failure",
             i.first, i.second);
    }
  }
  else
  {
    printf("No %s pages need to move.\n",
           page_type_to_migration_type[type]);
  }
}
