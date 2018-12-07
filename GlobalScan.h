#ifndef AEP_GLOBAL_SCAN_H
#define AEP_GLOBAL_SCAN_H

#include <vector>
#include <atomic>

#include "Queue.h"
#include "Process.h"
#include "EPTMigrate.h"
#include "BandwidthLimit.h"
#include "Numa.h"

enum JobIntent
{
  JOB_WALK,
  JOB_MIGRATE,
  JOB_QUIT,
};

typedef std::shared_ptr<EPTMigrate> EPTMigratePtr;

struct Job
{
  EPTMigratePtr migration;
  JobIntent intent;
};

class GlobalScan
{
  public:
    GlobalScan();

    void main_loop();
    void create_threads();
    void stop_threads();

    int collect();
    void walk_multi();
    void migrate();
    void count_refs();
    void count_migrate_stats();
    void update_interval(bool finished);
    void request_reload_conf();
    void apply_option();

  private:
    void consumer_loop();
    void walk_once();
    bool should_stop_walk();
    void update_dram_free_anon_bytes();
    void reload_conf();
    bool exit_on_stabilized();

    unsigned long accept_hot_bytes()   { return dram_free_anon_bytes * 3 / 4; }
    unsigned long target_young_bytes() { return dram_free_anon_bytes * 2 / 3; }
    unsigned long target_hot_bytes()   { return dram_free_anon_bytes / 2; }

    unsigned long get_dram_free_anon_bytes();

  private:
    static const int MAX_WALKS = 20;
    static const float MIN_INTERVAL;
    static const float MAX_INTERVAL;
    int nr_walks;
    int nr_acceptable_scans;
    float interval;
    float real_interval;
    struct timeval last_scan_start;
    unsigned long young_bytes;
    unsigned long top_bytes;
    unsigned long all_bytes;
    unsigned long dram_free_anon_bytes;

    ProcessCollection process_collection;
    std::vector<std::shared_ptr<EPTMigrate>> idle_ranges;
    std::vector<std::thread> worker_threads;
    Queue<Job> work_queue;
    Queue<Job> done_queue;

    std::atomic_int conf_reload_flag;

    BandwidthLimit throttler;
    NumaNodeCollection numa_collection;
};

#endif
// vim:set ts=2 sw=2 et:
