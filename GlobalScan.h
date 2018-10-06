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
    void update_interval();

  private:
    void consumer_loop();
    void walk_once();
    bool should_stop_walk();
    void do_walk(MigrationPtr migration);
    void account(MigrationPtr migration);
    void do_migrate(MigrationPtr migration);

  private:
    static const int NR_THREADS = 64;
    static const int MAX_WALKS = 30;
    int nr_walks;
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
