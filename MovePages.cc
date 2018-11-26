#include <numa.h>
#include <numaif.h>
#include <limits.h>
#include <sys/user.h>

#include "lib/stats.h"
#include "MovePages.h"
#include "Formatter.h"
#include "BandwidthLimit.h"

void MoveStats::clear()
{
  to_move_kb = 0;
  skip_kb = 0;
  move_kb = 0;
}

MovePages::MovePages() :
  flags(MPOL_MF_MOVE | MPOL_MF_SW_YOUNG),
  target_node(-1),
  page_shift(PAGE_SHIFT),
  batch_size(ULONG_MAX),
  throttler(NULL)
{
}

long MovePages::move_pages(std::vector<void *>& addrs)
{
  return move_pages(&addrs[0], addrs.size());
}

long MovePages::move_pages(void **addrs, unsigned long count)
{
  std::vector<int> nodes;
  const int *pnodes;
  long ret = 0;

  if (target_node >= 0) {
    // move pages
    nodes.resize(count, target_node);
    pnodes = &nodes[0];
  } else
    // locate pages
    pnodes = NULL;

  status.resize(count);

  ret = ::move_pages(pid, count, addrs, pnodes, &status[0], flags);
  if (ret)
    perror("move_pages");

  return ret;
}

long MovePages::locate_move_pages(std::vector<void *>& addrs, MoveStats *stats)
{
  unsigned long nr_pages = addrs.size();
  long ret = 0;
  MovePagesStatusCount status_sum;

  for (unsigned long i = 0; i < nr_pages; i += batch_size) {
    unsigned long size = std::min(batch_size, nr_pages - i);

    status.resize(nr_pages);

    int nid = target_node;
    target_node = -1; // cheat move_pages() to locate pages
    ret = move_pages(&addrs[i], size);
    target_node = nid;
    if (ret)
      break;

    clear_status_count();
    calc_status_count();
    account_stats(stats);
    add_status_count(status_sum);

    ret = move_pages(&addrs[i], size);
    if (ret)
      break;
  }

  return ret;
}

void MovePages::account_stats(MoveStats *stats)
{
  unsigned long skip_kb = 0;
  unsigned long move_kb = 0;
  int shift = page_shift - 10;

  for (auto &kv : status_count) {
    if (kv.first == target_node)
      skip_kb += kv.second << shift;
    else if (kv.first >= 0)
      move_kb += kv.second << shift;
  }

  stats->to_move_kb += status.size() << shift;
  stats->skip_kb += skip_kb;
  stats->move_kb += move_kb;

  // TODO: bandwidth limit on move_kb
  if (throttler)
    throttler->add_and_sleep(move_kb * 1024);
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
