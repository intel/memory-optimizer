#include <stdio.h>
#include <ctype.h>

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

std::unordered_map<std::string, MigrateWhat> Migration::migrate_name_map = {
	    {"none", MIGRATE_NONE},
	    {"hot",  MIGRATE_HOT},
	    {"cold", MIGRATE_COLD},
	    {"both", MIGRATE_BOTH},
};

Migration::Migration(ProcIdlePages& pip)
  : proc_idle_pages(pip), dram_percent(0)
{
  migrate_target_node.resize(PMD_ACCESSED + 1);
  migrate_target_node[PTE_IDLE]      = PMEM_NUMA_NODE;
  migrate_target_node[PTE_ACCESSED]  = DRAM_NUMA_NODE;

  migrate_target_node[PMD_IDLE]      = PMEM_NUMA_NODE;
  migrate_target_node[PMD_ACCESSED]  = DRAM_NUMA_NODE;
}

MigrateWhat Migration::parse_migrate_name(std::string name)
{
  if (isdigit(name[0])) {
    int m = atoi(name.c_str());
    if (m <= MIGRATE_BOTH)
      return (MigrateWhat)m;
    cerr << "invalid migrate type: " << name << endl;
    return MIGRATE_NONE;
  }

  auto search = migrate_name_map.find(name);

  if (search != migrate_name_map.end())
    return search->second;

  cerr << "invalid migrate type: " << name << endl;
  return MIGRATE_NONE;
}

int Migration::set_dram_percent(int dp)
{
  if (dp < 0 || dp > 100) {
    cerr << "dram percent out of range [0, 100]" << endl;
    return -ERANGE;
  }

  dram_percent = dp;
  return 0;
}

size_t Migration::get_threshold_refs(ProcIdlePageType type,
                                     int& min_refs, int& max_refs)
{
  const page_refs_map& page_refs = proc_idle_pages.get_pagetype_refs(type).page_refs;
  int nr_walks = proc_idle_pages.get_nr_walks();
  vector<unsigned long> refs_count = proc_idle_pages.get_pagetype_refs(type).refs_count;

  double ratio;

  if (dram_percent) {
    if (migrate_target_node[type] == DRAM_NUMA_NODE)
      ratio = dram_percent / 100.0;
    else
      ratio = (100.0 - dram_percent) / 100.0;
  } else
    ratio = (double) proc_vmstat.anon_capacity(migrate_target_node[type]) / proc_vmstat.anon_capacity();

  // XXX: this assumes all processes have same hot/cold distribution
  size_t portion = page_refs.size() * ratio;
  long quota = portion;

  printf("migrate ratio: %.2f = %lu / %lu\n", ratio, portion, page_refs.size());

  if (type & PAGE_ACCESSED_MASK) {
    min_refs = nr_walks;
    max_refs = nr_walks;
    for (; min_refs > nr_walks / 2; --min_refs) {
      quota -= refs_count[min_refs];
      if (quota <= 0)
        break;
    }
    if (min_refs < nr_walks)
      ++min_refs;
  } else {
    min_refs = 0;
    max_refs = 0;
    for (; max_refs < nr_walks / 2; ++max_refs) {
      quota -= refs_count[max_refs];
      if (quota <= 0)
        break;
    }
    max_refs >>= 1;
  }

  printf("refs range: %d-%d\n", min_refs, max_refs);

  return portion;
}

int Migration::select_top_pages(ProcIdlePageType type)
{
  const page_refs_map& page_refs = proc_idle_pages.get_pagetype_refs(type).page_refs;
  int min_refs;
  int max_refs;

  if (page_refs.empty())
    return 1;

  get_threshold_refs(type, min_refs, max_refs);

  for (auto it = page_refs.begin(); it != page_refs.end(); ++it) {
    printdd("vpfn: %lx count: %d\n", it->first, (int)it->second);
    if (it->second >= min_refs &&
        it->second <= max_refs)
      pages_addr[type].push_back((void *)(it->first << PAGE_SHIFT));
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
  auto& addrs = pages_addr[type];
  int ret;

  ret = do_move_pages(type, NULL);
  if (ret)
    return ret;

  size_t j = 0;
  for (size_t i = 0; i < addrs.size(); ++i) {
    if (migrate_status[i] >= 0 &&
        migrate_status[i] != migrate_target_node[type])
      addrs[j++] = addrs[i];
  }

  addrs.resize(j);

  return 0;
}

int Migration::migrate(ProcIdlePageType type)
{
  std::vector<int> nodes;
  int ret;

  ret = select_top_pages(type);
  if (ret)
    return ret;

  ret = locate_numa_pages(type);
  if (ret)
    return ret;

  nodes.clear();
  nodes.resize(pages_addr[type].size(), migrate_target_node[type]);

  ret = do_move_pages(type, &nodes[0]);
  return ret;
}

long Migration::__move_pages(pid_t pid, unsigned long nr_pages,
                             void **addrs, const int *nodes)
{
  long ret = 0;

  migrate_status.resize(nr_pages);

  unsigned long batch_size = 1 << 12;
  for (unsigned long i = 0; i < nr_pages; i += batch_size) {
    ret = move_pages(pid,
                     min(batch_size, nr_pages - i),
                     addrs + i,
                     nodes ? nodes + i : NULL,
                     &migrate_status[i], MPOL_MF_MOVE);
    if (ret) {
      perror("move_pages");
      break;
    }
  }

  return ret;
}

long Migration::do_move_pages(ProcIdlePageType type, const int *nodes)
{
  pid_t pid = proc_idle_pages.get_pid();
  auto& addrs = pages_addr[type];
  long nr_pages = addrs.size();
  long ret;

  ret = __move_pages(pid, nr_pages, &addrs[0], nodes);
  if (!ret)
    show_migrate_stats(type, nodes ? "after migrate" : "before migrate");

  return ret;
}

std::unordered_map<int, int> Migration::calc_migrate_stats()
{
  std::unordered_map<int, int> stats;

  for(int &i : migrate_status)
    inc_count(stats, i);

  return stats;
}

void Migration::show_numa_stats()
{
  proc_vmstat.load_vmstat();
  proc_vmstat.load_numa_vmstat();

  const auto& numa_vmstat = proc_vmstat.get_numa_vmstat();
  unsigned long total_anon_kb = proc_vmstat.vmstat("nr_inactive_anon") +
                                proc_vmstat.vmstat("nr_active_anon") +
                                proc_vmstat.vmstat("nr_isolated_anon");

  total_anon_kb *= PAGE_SIZE >> 10;
  printf("%'15lu       anon total\n", total_anon_kb);

  int nid = 0;
  for (auto& map: numa_vmstat) {
    unsigned long anon_kb = map.at("nr_inactive_anon") +
                            map.at("nr_active_anon") +
                            map.at("nr_isolated_anon");
    anon_kb *= PAGE_SIZE >> 10;
    printf("%'15lu  %2d%%  anon node %d\n", anon_kb, percent(anon_kb, total_anon_kb), nid);
    ++nid;
  }
}

void Migration::show_migrate_stats(ProcIdlePageType type, const char stage[])
{
    unsigned long total_kb = proc_idle_pages.get_pagetype_refs(type).page_refs.size() * (pagetype_size[type] >> 10);
    unsigned long to_migrate = pages_addr[type].size() * (pagetype_size[type] >> 10);

    printf("    %s: %s\n", pagetype_name[type], stage);

    show_numa_stats();

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

void Migration::fill_addrs(std::vector<void *>& addrs, unsigned long start)
{
    void **p = &addrs[0];
    void **pp = &addrs[addrs.size()-1];

    for (; p <= pp; ++p) {
      *p = (void *)start;
      start += PAGE_SIZE;
    }
}

void Migration::dump_node_percent()
{
  auto stats = calc_migrate_stats();
  size_t nr_node0 = (size_t)stats[0];
  size_t nr_err = 0;

  for(auto &kv : stats)
  {
    int status = kv.first;

    if (status < 0)
      ++nr_err;
  }

  printf("%3u ", percent(nr_node0, migrate_status.size()));
  if (nr_err)
    printf("(-%u) ", percent(nr_err, migrate_status.size()));
}

int Migration::dump_vma_nodes(proc_maps_entry& vma)
{
  pid_t pid = proc_idle_pages.get_pid();
  unsigned long nr_pages;
  int err = 0;

  if (vma.end - vma.start < 1<<30)
    return 0;

  nr_pages = (vma.end - vma.start) >> PAGE_SHIFT;

  unsigned long total_kb = (vma.end - vma.start) >> 10;
  printf("VMA size: %'15lu \n", total_kb);

  const int nr_slots = 10;
  unsigned long slot_pages = nr_pages / nr_slots;

  std::vector<void *> addrs;
  addrs.resize(slot_pages);
  migrate_status.resize(slot_pages);

  for (int i = 0; i < nr_slots; ++i)
  {
    fill_addrs(addrs, vma.start + i * addrs.size() * PAGE_SIZE);

    err = move_pages(pid,
                     slot_pages,
                     &addrs[0],
                     NULL,
                     &migrate_status[0], MPOL_MF_MOVE);
    if (err) {
      perror("move_pages");
      return err;
    }

    dump_node_percent();
  }

  return err;
}

int Migration::dump_task_nodes()
{
  pid_t pid = proc_idle_pages.get_pid();
  ProcMaps proc_maps;
  int err = 0;

  auto maps = proc_maps.load(pid);

  for (auto &vma: maps) {
    err = dump_vma_nodes(vma);
    if (err)
      break;
  }

  return err;
}

