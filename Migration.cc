#include <stdio.h>
#include <map>
#include <string>
#include <iostream>
#include <algorithm>
#include <sys/mman.h>

#include <numa.h>
#include <numaif.h>
#include "Migration.h"

using namespace std;

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
  cout << "nr_pages: " << nr_pages;

  for (auto it = page_refs.begin();
       it != page_refs.end(); ++it) {
    cout << "va: " << it->first << "count: " << it->second;

    if (it->second >= nr_walks)
      pages_addr[type].push_back((void *)it->first);
  }

  sort(pages_addr[type].begin(), pages_addr[type].end());

  // just for debug
  for (int i = 0; i < nr_pages; ++i) {
    cout << "page " << i << ": " << pages_addr[type][i];
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
  int node, ret = 0;
  vector<void *>::iterator it;

  auto& params = policies[type];
  auto& addrs = pages_addr[type];
  auto& nodes = pages_node[type];

  // Retrieves numa node for the given page.
  // XXX: this costs lots of syscalls, use move_pages() will nodes=NULL instead
  for (it = addrs.begin(); it < addrs.end();) {
    cout << "it: " << *it;
    ret = get_mempolicy(&node, NULL, 0,
                        *it, MPOL_F_NODE | MPOL_F_ADDR);
    if (ret) {
        cout << "get_mempolicy return %d" << ret;
        return ret;
    }
    if (node == params.node) {
      //don't need to migrate
      //the hot page in hot memory or the cold page in cold memory
      it = addrs.erase(it);
    } else {
      nodes.push_back(params.node);
      ++it;
    }
  }

  return 0;
}

// migrate pages to nodes
int Migration::migrate(ProcIdlePageType type)
{
  int nr_pages = 0;
  int ret = 0;
  pid_t pid = proc_idle_pages.get_pid();

  ret = select_top_pages(type);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }

  ret = locate_numa_pages(type);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }

  auto& addrs = pages_addr[type];
  auto& nodes = pages_node[type];

  nr_pages = addrs.size();
  cout << "nr_pages: " << nr_pages;

  migrate_status.resize(nr_pages);
  ret = move_pages(pid, nr_pages, &addrs[0], &nodes[0],
                   &migrate_status[0], MPOL_MF_MOVE);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }

  return ret;
}
