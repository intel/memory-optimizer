#ifndef _BANDWIDTH_LIMIT_H
#define _BANDWIDTH_LIMIT_H

#include <atomic>

class BandwidthLimit
{
  public:
    void set_bwlimit_mbps(unsigned long mbps) { bwlimit_mbps = mbps; }
    void add_and_sleep(unsigned long bytes);

  private:
    unsigned long bwlimit_mbps;
    std::atomic_ulong bytes;
    // add: time recording vars
}

#endif
// vim:set ts=2 sw=2 et:
