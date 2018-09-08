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

bool descend_by_value(void *lhs, void *rhs)
{
	return (unsigned long)lhs > (unsigned long)rhs;
}

Migration::Migration()
{
  memset(&policies, 0, sizeof(policies));
}

int Migration::walk(
		std::unordered_map<unsigned long,unsigned char>& page_refs)
{
  int nr_pages, i;

  nr_pages = page_refs.size();
  cout << "nr_pages: " << nr_pages;

  for (auto it = page_refs.begin();
       it != page_refs.end(); ++it) {
    cout << "va: " << it->first << "count: " << it->second;

    // insert to the end
    pages_addr[0].push_back((void *)it->first);
    pages_addr[1].push_back((void *)it->first);
  }

  sort(pages_addr[0].begin(), pages_addr[0].end(), descend_by_value);
  sort(pages_addr[1].begin(), pages_addr[1].end());

  // just for debug
  for (i = 0; i < nr_pages; ++i) {
    cout << "hot page " << i << ": " << pages_addr[0][i];
    cout << "cold page " << i << ": " << pages_addr[1][i];
  }

  return 0;
}

int Migration::set_policy(int samples_percent, int pages_percent,
                          int node, MigrateType type)
{
  policies[type].nr_samples_percent = samples_percent;
  policies[type].nr_pages_percent = pages_percent;
  policies[type].node = node;

  return 0;
}

int Migration::locate_numa_pages(MigrateType type)
{
  int node, ret = 0;
  vector<void *>::iterator it;

  auto& params = policies[type];
  auto& addrs = pages_addr[type];
  auto& nodes = pages_node[type];

  //Retrieves numa node for the given page.
  for (it = addrs.begin(); it != addrs.end(); ++it) {
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
      addrs.erase(it);
    } else {
      migrate_status.push_back(0);
      nodes.push_back(params.node);
    }
  }

  return 0;
}

// migrate pages to nodes
int Migration::migrate(pid_t pid,
                       std::unordered_map<unsigned long, unsigned char>& page_refs,
                       std::vector<int>& status,
                       unsigned long nr_walks,
                       MigrateType type)
{
  int nr_pages = 0;
  int ret = 0;

  ret = walk(page_refs);
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

  ret = move_pages(pid, nr_pages, &addrs[0], &nodes[0],
                   &status[0], MPOL_MF_MOVE);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }

  return ret;
}
