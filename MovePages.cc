#include <numa.h>
#include <numaif.h>
#include <limits.h>
#include <sys/user.h>

#include "lib/stats.h"
#include "MovePages.h"
#include "Formatter.h"

MovePages::MovePages() :
  flags(MPOL_MF_MOVE | MPOL_MF_SW_YOUNG),
  target_node(-1),
  page_shift(PAGE_SHIFT),
  batch_size(ULONG_MAX)
{
}

long MovePages::move_pages(std::vector<void *>& addrs)
{
  std::vector<int> nodes;
  const int *pnodes;
  unsigned long count = addrs.size();
  long ret = 0;

  if (target_node >= 0) {
    // move pages
    nodes.resize(count, target_node);
    pnodes = &nodes[0];
  } else
    // locate pages
    pnodes = NULL;

  status.resize(count);

  ret = ::move_pages(pid, count, &addrs[0], pnodes, &status[0], flags);
  if (ret)
    perror("move_pages");

  if (!ret)
    calc_status_count();

  return ret;
}

void MovePages::calc_status_count()
{
  for (int &i : status)
    inc_count(status_count, i);
}

void MovePages::add_status_count(MovePagesStatusCount& status_sum)
{
  for (auto &kv : status_count)
    add_count(status_sum, kv.first, kv.second);
}

void MovePages::show_status_count(Formatter* fmt)
{
  show_status_count(fmt, status_count);
}

void MovePages::show_status_count(Formatter* fmt, MovePagesStatusCount& status_sum)
{
  unsigned long total_kb = 0;

  for (auto &kv : status_sum)
    total_kb += kv.second;
  total_kb <<= page_shift - 10;

  for (auto &kv : status_sum)
  {
    int nid = kv.first;
    unsigned long kb = kv.second << (page_shift - 10);

    if (nid >= 0)
      fmt->print("%'15lu  %2d%%  node %d\n", kb, percent(kb, total_kb), nid);
    else
      fmt->print("%'15lu  %2d%%  %s\n", kb, percent(kb, total_kb), strerror(-nid));
  }
}
