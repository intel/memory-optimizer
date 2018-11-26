#ifndef _BANDWIDTH_LIMIT_H
#define _BANDWIDTH_LIMIT_H

#include <atomic>
#include <mutex>
#include <sys/time.h>

class BandwidthLimit
{
  public:
    void set_bwlimit_mbps(float mbps)
    { bwlimit_byteps = (mbps * 1024 * 1024) / 8; }

    void add_and_sleep(unsigned long bytes);

  private:
    unsigned long bwlimit_byteps = 0;
    long allow_bytes = 0;

    timeval last_time = {0,0};

    std::mutex mlock;
};

#endif
// vim:set ts=2 sw=2 et:
