#include <numa.h>
#include <numaif.h>
#include <limits.h>
#include <sys/user.h>
#include <utility>

#include "lib/stats.h"
#include "MovePages.h"
#include "Formatter.h"
#include "BandwidthLimit.h"
#include "Numa.h"

void MoveStats::clear()
{
  to_move_kb = 0;
  skip_kb = 0;
  move_kb = 0;
  move_page_status.clear();
}

void MoveStats::add(MoveStats* s)
{
  to_move_kb += s->to_move_kb;
  skip_kb += s->skip_kb;
  move_kb += s->move_kb;
}

void MoveStats::save_move_states(std::vector<int>& status,
                                 std::vector<int>& target_nodes,
                                 std::vector<int>& status_after_move)
{
  int key;

  for (size_t i = 0; i < status.size(); ++i) {

    // let's what will return if negative value in
    //if (status[i] < 0)
    //  continue;
    key = (status[i] << from_shift)
      + (target_nodes[i] << to_shift)
      + (status_after_move[i] << result_shift);
    add_count(move_page_status, key, 1);
  }

  return;
}

void MoveStats::show_move_state(Formatter& fmt)
{
  signed char from, to, result;
  int key;

  for (auto& i : move_page_status) {
    key = i.first;
    from = (key >> from_shift) & 0xff;
    to = (key >> to_shift) & 0xff;
    result = (key >> result_shift) & 0xff;

    fmt.print("from node %d to node %d: %d pages with result %d\n",
              (int)from, (int)to, i.second, (int)result);
  }
}


MovePages::MovePages() :
  flags(MPOL_MF_MOVE | MPOL_MF_SW_YOUNG),
  page_shift(PAGE_SHIFT),
  batch_size(ULONG_MAX),
  throttler(NULL)
{
}

long MovePages::move_pages(std::vector<void *>& addrs, bool is_locate)
{
  return move_pages(&addrs[0], addrs.size(), is_locate);
}

long MovePages::move_pages(void **addrs, unsigned long count, bool is_locate)
{
  std::vector<int> nodes;
  const int *pnodes;
  long ret = 0;

  if (!is_locate) {
    // move pages
    pnodes = &target_nodes[0];
    status_after_move.resize(count);
    ret = ::move_pages(pid, count, addrs, pnodes, &status_after_move[0], flags);
  } else {
    // locate pages
    pnodes = NULL;
    status.resize(count);
    ret = ::move_pages(pid, count, addrs, pnodes, &status[0], flags);
  }

  if (ret < 0)
    perror("WARNING: move_pages");
  else if (ret > 0)
    fprintf(stderr, "WARNING: move_pages failed %ld\n", ret);

  return ret;
}

long MovePages::locate_move_pages(std::vector<void *>& addrs,
                                  MoveStats *stats)
{
  unsigned long nr_pages = addrs.size();
  long ret = 0;
  MovePagesStatusCount status_sum;

  for (unsigned long i = 0; i < nr_pages; i += batch_size) {
    unsigned long size = std::min(batch_size, nr_pages - i);

    status.resize(nr_pages);

    // locate pages
    ret = move_pages(&addrs[i], size, true);
    if (ret) {
      fprintf(stderr, "WARNING: locate pages failed %ld", ret);
      break;
    }

    calc_target_nodes(addrs, size);
    clear_status_count();
    calc_status_count();
    account_stats(stats);
    add_status_count(status_sum);

    if (debug_level() >= 3)
      dump_target_nodes();

    ret = move_pages(&addrs[i], size, false);
    if (ret >= 0)
      stats->save_move_states(status, target_nodes, status_after_move);
    if (ret) {
      fprintf(stderr, "WARNING: move pages failed %ld", ret);
      continue;
    }
  }

  return ret;
}

void MovePages::account_stats(MoveStats *stats)
{
  unsigned long skip_kb = 0;
  unsigned long move_kb = 0;
  const int shift = page_shift - 10;

  for (auto &kv : status_count) {
    // Don't calculate invalid pages.
    // The status includes "Bad address or No such file" which return by move_pages.
    // Maybe these pages have no corresponding physical memory.
    if (kv.first < 0)
      continue;
    if (is_node_in_target_set(kv.first))
      skip_kb += kv.second << shift;
    else if (kv.first >= 0)
      move_kb += kv.second << shift;
  }

  stats->to_move_kb += skip_kb + move_kb;
  stats->skip_kb += skip_kb;
  stats->move_kb += move_kb;

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
    // skip invalid pages
    if (kv.first >= 0)
      total_kb += kv.second;
  total_kb <<= page_shift - 10;

  for (auto &kv : status_sum)
  {
    int nid = kv.first;
    unsigned long kb = kv.second << (page_shift - 10);

    if (nid >= 0)
      fmt->print("%'15lu  %2d%%  node %d\n", kb, percent(kb, total_kb), nid);
    else if (debug_level())
      fmt->print("%'15lu  %2d%%  %s\n", kb, percent(kb, total_kb), strerror(-nid));
  }
}

void MovePages::calc_target_nodes(std::vector<void *>& addrs, long size)
{
  NumaNode* numa_obj;
  int last_good_index;
  int i = 0;

  target_nodes.resize(size);

  /*
    in status:
    0, -14, 0, -2, -2 => 0, 0, -14, -2, -2

    also changed the corresponding value in addrs
   */
  while(i < size) {
    if (status[i] >= 0) {
      numa_obj = numa_collection->get_node(status[i]);
      target_nodes[i] = get_target_node(numa_obj);
      ++i;
    } else {
      last_good_index = find_last_good(status, i);
      if (last_good_index == i)
        break;

      std::swap(addrs[i], addrs[last_good_index]);
      std::swap(status[i], status[last_good_index]);
    }
  }
}

int MovePages::get_target_node(NumaNode* node_obj)
{
  NumaNode* peer_node;
  bool is_dram_to_dram, is_pmem_to_pmem;

  if (!node_obj) {
    fprintf(stderr, "get_target_node() node_obj = NULL\n");
    return -1;
  }

  is_dram_to_dram = (type < MAX_ACCESSED)
                    && !node_obj->is_pmem();
  is_pmem_to_pmem = (!(type < MAX_ACCESSED))
                    && node_obj->is_pmem();

  if (is_dram_to_dram || is_pmem_to_pmem)
    return node_obj->id();

  peer_node = node_obj->get_peer_node();
  if (peer_node)
    return peer_node->id();

  fprintf(stderr, "get_target_node() failed\n");
  return -2;
}

bool MovePages::is_node_in_target_set(int node_id)
{
  NumaNode* numa_obj;

  if (node_id < 0) {
    fprintf(stderr, "is_node_in_target_set(): unexpected  node_id %d \n",
            node_id);
    return false;
  }

  numa_obj = numa_collection->get_node(node_id);
  if (!numa_obj) {
    fprintf(stderr, "is_node_in_target_set(): non-exist node_id %d \n",
            node_id);
    return false;
  }

  if (type < MAX_ACCESSED)
    return !numa_obj->is_pmem();
  else
    return numa_obj->is_pmem();
}

long MovePages::find_last_good(std::vector<int>& status, long end_pos)
{
  for (long i = status.size() - 1; i > end_pos; --i) {
    if (status[i] >= 0)
      return i;
  }
  return end_pos;
}

void MovePages::dump_target_nodes(void)
{
  size_t end = std::min(target_nodes.size(), status.size());

  for (size_t i = 0; i < end; ++i) {
    printf("%d -> %d\n", status[i], target_nodes[i]);
  }
}
