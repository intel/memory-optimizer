#ifndef AEP_ADDR_SEQUENCE_H
#define AEP_ADDR_SEQUENCE_H

#include <map>
#include <vector>

struct DeltaPayload
{
  uint8_t delta;    // in pagesize unit
  uint8_t payload;  // stores refs count
}

struct AddrCluster
{
  unsigned long start;
  int size;
  DeltaPayload *deltas; // points into AddrSequence::bufs
}

// A sparse array for ordered addresses.
// There will be 2 such arrays, one for 4K pages, another or 2M pages.
//
// Since addresses will have good spacial locality, organize them by clusters.
// Each AddrCluster represents a range of addresses that are close enough to
// each other, within 255 pages distance from prev/next neighbors, thus can be
// encoded by 8bit delta to save space.
//
// The addresses will be appended, updated or visited all sequentially.
class AddrSequence
{
  public:
    AddrSequence();
    size_t size() { return size; }
    bool empty()  { return size > 0; }

    void set_pageshift(int shift);
    void clear();

    // call me before starting each walk
    int rewind();

    // n=0: [addr]=0 if not already set
    // n=1: [addr]=1 if not already set, [addr]++ if already there
    //
    // Depending on nr_walks, it'll behave differently
    // - appending: for the first walk
    // will add addresses and set payload to 0 or 1
    // will allocate bufs
    //
    // - updating: for (nr_walks >= 2) walks
    // will do ++payload
    // will ignore addresses not already there
    int inc_payload(unsigned long addr, int n);

    // for sequential visiting
    int get_first(unsigned long& addr, uint8_t& payload);
    int get_next(unsigned long& addr, uint8_t& payload);

  private:
    const int BUF_SIZE = 0x10000; // 64KB;

    int nr_walks;
    int pageshift;
    int pagesize;
    unsigned long size;  // # of addrs stored

    std::map<unsigned long, AddrCluster> addr_clusters;

    // inc_payload() will allocate new fixed size buf on demand,
    // avoiding internal/external fragmentations.
    // Only freed on clear().
    std::vector<std::array<uint8_t, BUF_SIZE>> bufs;
}

#endif
// vim:set ts=2 sw=2 et:
