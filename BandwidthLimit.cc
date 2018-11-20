#include <unistd.h>

#include "BandwidthLimit.h"
#include "lib/stats.h"


void BandwidthLimit::add_and_sleep(unsigned long bytes)
{
    float time_delta;
    timeval cur_time;

    if (bwlimit_mbps < 0)
        return;

    if (!last_time.tv_sec && !last_time.tv_usec) {
        gettimeofday(&last_time, NULL);
        return;
    }

    std::unique_lock<std::mutex> lock(mlock);

    gettimeofday(&cur_time, NULL);
    time_delta = tv_secs(last_time, cur_time);
    last_time = cur_time;

    bytes += time_delta * bwlimit_mbps;
    bytes -= bytes;

    if (bytes < 0) {
        unsigned int sleep_time;
        sleep_time = ((-bytes)/bwlimit_mbps) * 1000000;
        usleep(sleep_time);
    }
}
