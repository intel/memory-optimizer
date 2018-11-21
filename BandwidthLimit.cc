#include <unistd.h>
#include <string.h>
#include <vector>
#include <thread>
#include <iostream>

#include "BandwidthLimit.h"
#include "lib/stats.h"


void BandwidthLimit::add_and_sleep(unsigned long bytes)
{
    float time_delta;
    timeval cur_time;

    if (bwlimit_mbps < 0)
        return;

    if (!last_time.tv_sec && !last_time.tv_usec) {
        allow_bytes = 0;
        gettimeofday(&last_time, NULL);
        return;
    }

    std::unique_lock<std::mutex> lock(mlock);

    gettimeofday(&cur_time, NULL);
    time_delta = tv_secs(last_time, cur_time);
    last_time = cur_time;

    allow_bytes += time_delta * bwlimit_mbps;
    allow_bytes -= bytes;

    if (allow_bytes < 0) {
        float sleep_time;
        sleep_time = ((-allow_bytes) / bwlimit_mbps) * 1000000;
        printf("thread will sleep %f microseconds\n", sleep_time);
        usleep(sleep_time);
    }
}


#ifdef _BANDWIDTHLIMIT_SELF_TEST_

static BandwidthLimit limiter;
static std::atomic_long  total_bytes;
static float  total_time_cost;
static std::vector<std::thread> thread_pool;
static float speed;
static timeval time_start, time_end;
static std::mutex  time_mutex;

void prepare_test()
{
    total_bytes = 0;
    total_time_cost = 0;
    thread_pool.clear();
    memset(&time_start, 0, sizeof(time_start));
    memset(&time_end, 0, sizeof(time_end));
}


void update_time_record()
{
    time_mutex.lock();

    if (!time_start.tv_sec
        && !time_start.tv_usec) {
        gettimeofday(&time_start, NULL);
    } else {
        gettimeofday(&time_end, NULL);
    }

    time_mutex.unlock();
}

unsigned long  work_emulator(unsigned int max_sleep_ms,
                             unsigned long max_work)
{
    unsigned int sleep_time = (rand() % max_sleep_ms) + 1;
    unsigned long work_bytes = (rand() % max_work) + 1;

    usleep(sleep_time * 1000);

    return work_bytes;
}


void worker()
{
    unsigned long work_bytes;
    long local_total_bytes = 0;

    update_time_record();
    for (int i = 0; i < 10; ++i) {

        work_bytes = work_emulator(1000, 1000);
        limiter.add_and_sleep(work_bytes);

        local_total_bytes += work_bytes;
    }
    update_time_record();

    std::atomic_fetch_add(&total_bytes, local_total_bytes);
}


void calc_speed()
{
    total_time_cost = tv_secs(time_start, time_end);
    speed = total_bytes/total_time_cost;
}


void thread_worker()
{
    printf("thread_worker created\n");
    worker();
}


void multi_thread_test(int max_thread)
{
    printf("%d thread(s) case started:\n", max_thread);

    limiter.set_bwlimit_mbps(100 * 8);

    thread_pool.reserve(max_thread);
    for (size_t i = 0; i < max_thread; ++i)
        thread_pool.push_back(std::thread(&thread_worker));

    for (auto& each : thread_pool)
        each.join();

    calc_speed();
    printf("speed = %f\n", speed);
}


int main(int argc, char* argv[])
{
    prepare_test();
    multi_thread_test(1);

    prepare_test();
    multi_thread_test(2);

    prepare_test();
    multi_thread_test(3);

    prepare_test();
    multi_thread_test(4);

    prepare_test();
    multi_thread_test(5);

    return 0;
}


#endif
