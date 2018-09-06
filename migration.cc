#include <stdio.h>
#include <map>
#include <string>
#include <iostream>
#include <algorithm>
#include <sys/mman.h>

#include <numa.h>
#include <numaif.h>
#include "migration.h"

using namespace std;

bool descend_by_value(void *lhs, void *rhs)
{
	return (unsigned long)lhs > (unsigned long)rhs;
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
    hot_pages.push_back((void *)it->first);
    cold_pages.push_back((void *)it->first);
  }

  sort(hot_pages.begin(), hot_pages.end(), descend_by_value);
  sort(cold_pages.begin(), cold_pages.end());

  // just for debug
  for (i = 0; i < nr_pages; ++i) {
    cout << "hot page " << i << ": " << hot_pages[i];
    cout << "cold page " << i << ": " << cold_pages[i];
  }

  return 0;
}

int Migration::set_policy(int samples_percent, int pages_percent,
                          int node, bool hot)
{
  if (hot) {
    hot_policy.nr_samples_percent = samples_percent;
    hot_policy.nr_pages_percent = pages_percent;
    hot_policy.node = node;
  } else {
    cold_policy.nr_samples_percent = samples_percent;
    cold_policy.nr_pages_percent = pages_percent;
    cold_policy.node = node;
  }

  return 0;
}

int Migration::get_pages(std::vector<void *>& pages,
					     std::vector<int>& nodes,
                         bool hot)
{
  unsigned int i = 0;
  int node, ret = 0;
  vector<void *>::iterator it;

  //Retrieves numa node for the given page.
  for (it = pages.begin(); it != pages.end(); ++it, ++i)
    cout << "it: " << *it;
    ret = get_mempolicy(&node, NULL, 0,
                        *it, MPOL_F_NODE | MPOL_F_ADDR);
    if (ret) {
        cout << "get_mempolicy return %d" << ret;
        return ret;
    }
    if ((hot && (node == hot_policy.node)) ||
        (!hot && (node == cold_policy.node))) {
      //don't need to migrate
      //the hot page in hot memory or the cold page in cold memory
      pages.erase(it);
    } else {
      migrate_status.push_back(0);
      if (hot)
        nodes.push_back(hot_policy.node);
      else
        nodes.push_back(cold_policy.node);
    }

    return 0;
}

// migrate pages to nodes
int Migration::migrate(
            pid_t pid,
            std::unordered_map<unsigned long, unsigned char>& page_refs,
            std::vector<int>& status,
            unsigned long nr_walks,
            bool hot)
{
  int hot_nr_pages, cold_nr_pages;
  int nr_samples, nr_pages;
  int ret = 0;

  ret = walk(page_refs);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }

  ret = get_pages(hot_pages, hot_nodes, true);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }

  hot_nr_pages = hot_pages.size();
  cout << "hot_nr_pages: " << hot_nr_pages;

  nr_samples = nr_walks * hot_policy.nr_samples_percent / 100;
  nr_pages = hot_nr_pages * hot_policy.nr_pages_percent / 100;
  hot_nr_pages = min(nr_samples, nr_pages);

  cout << "nr_samples: " << nr_samples << "nr_pages: " << nr_pages;
  cout << "nr_walks: " << nr_walks << "hot_nr_pages: " << hot_nr_pages;

  ret = move_pages(pid, hot_nr_pages, &hot_pages[0], &hot_nodes[0],
                   &status[0], MPOL_MF_MOVE);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }

  // migrate for cold pages ?
#if 1
  ret = get_pages(cold_pages, cold_nodes, true);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }

  cold_nr_pages = cold_pages.size();
  cout << "cold_nr_pages: " << cold_nr_pages;

  nr_samples = nr_walks * cold_policy.nr_samples_percent / 100;
  nr_pages = cold_nr_pages * cold_policy.nr_pages_percent / 100;
  cold_nr_pages = min(nr_samples, nr_pages);

  cout << "nr_samples: " << nr_samples << "nr_pages: " << nr_pages;
  cout << "nr_walks: " << nr_walks << "cold_nr_pages: " << cold_nr_pages;

  ret = move_pages(pid, cold_nr_pages, &cold_pages[0], &cold_nodes[0],
                   &status[0], MPOL_MF_MOVE);
  if (ret) {
    cout << "error: return " << ret;
    return ret;
  }
#endif

  return ret;
}
