#include <unistd.h>
#include <string.h>
#include <vector>
#include <thread>
#include <iostream>

#include "BandwidthLimit.h"
#include "lib/stats.h"

const float BandwidthLimit::MAX_TIME_HISTORY = 3;

void BandwidthLimit::add_and_sleep(unsigned long bytes)
{
    float time_delta;
    timeval cur_time;

    if (0 == bwlimit_byteps)
        return;

    if (!last_time.tv_sec && !last_time.tv_usec) {
        allow_bytes = 0;
        gettimeofday(&last_time, NULL);
        return;
    }

    std::unique_lock<std::mutex> lock(mlock);

    gettimeofday(&cur_time, NULL);
    time_delta = tv_secs(last_time, cur_time);
    if (time_delta > MAX_TIME_HISTORY)
        time_delta = MAX_TIME_HISTORY;

    last_time = cur_time;

    allow_bytes += time_delta * bwlimit_byteps;
    allow_bytes -= bytes;

    if (allow_bytes < 0) {
        float sleep_time;
        sleep_time = ((-allow_bytes) / bwlimit_byteps) * 1000000;
        printf("thread will sleep %f microseconds\n", sleep_time);
        usleep(sleep_time);
    }
}
