#ifndef _BANDWIDTH_LIMIT_H
#define _BANDWIDTH_LIMIT_H

#include <atomic>
#include <mutex>
#include <sys/time.h>

class BandwidthLimit
{
  public:
    void set_bwlimit_mbps(unsigned long mbps) { bwlimit_mbps = mbps/8.0; }
    void add_and_sleep(unsigned long bytes);

  private:
    float bwlimit_mbps = -1.0;
    std::atomic_ulong bytes;
    timeval last_time = {0,0};

    std::mutex mlock;
};

#endif
// vim:set ts=2 sw=2 et:
