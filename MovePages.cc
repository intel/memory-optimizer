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

  return ret;
}

void MovePages::calc_status_count()
{
  for (int &i : status)
    inc_count(status_count, i);
}

