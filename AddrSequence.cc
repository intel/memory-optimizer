#include "AddrSequence.h"

void AddrSequence::AddrSequence()
{
  size = 0;
  nr_walks = 0;
  pageshift = 0;
  pagesize = 0;
}

void AddrSequence::clear()
{
  size = 0;
  nr_walks = 0;
  addr_clusters.clear();
  bufs.clear();
}

void AddrSequence::set_pageshift(int shift)
{
  pageshift = shift;
  pagesize = 1 << shift;
}

int AddrSequence::rewind()
{
  ++nr_walks;
}

int AddrSequence::inc_payload(unsigned long addr, int n)
{
}

int AddrSequence::get_first(unsigned long& addr, uint8_t& payload)
{
}

int AddrSequence::get_next(unsigned long& addr, uint8_t& payload)
{
}
