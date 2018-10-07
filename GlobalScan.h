#ifndef AEP_GLOBAL_SCAN_H
#define AEP_GLOBAL_SCAN_H

#include <vector>

#include "Queue.h"
#include "Process.h"
#include "Migration.h"

enum JobIntent
{
  JOB_WALK,
  JOB_MIGRATE,
  JOB_QUIT,
};

typedef std::shared_ptr<Migration> MigrationPtr;

struct Job
{
  MigrationPtr migration;
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
    void update_interval(bool finished);

  private:
    void consumer_loop();
    void walk_once();
    bool should_stop_walk();
    void do_walk(MigrationPtr migration);
    void do_migrate(MigrationPtr migration);
    void gather_walk_stats(MigrationPtr migration);

  private:
    static const int NR_THREADS = 64;
    static const int MAX_WALKS = 30;
    static const float MIN_INTERVAL;
    static const float MAX_INTERVAL;
    static const float INITIAL_INTERVAL;
    int nr_walks;
    float interval;
    unsigned long young_bytes;
    unsigned long top_bytes;
    unsigned long all_bytes;

    ProcessCollection process_collection;
    std::vector<std::shared_ptr<Migration>> idle_ranges;
    std::vector<std::thread> worker_threads;
    Queue<Job> work_queue;
    Queue<Job> done_queue;
};

#endif
// vim:set ts=2 sw=2 et:
