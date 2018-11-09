#ifndef AEP_PROCESS_H
#define AEP_PROCESS_H

#include <memory>
#include <vector>
#include <unordered_map>

#include "ProcPid.h"
#include "ProcMaps.h"
#include "ProcStatus.h"
#include "Option.h"

class Migration;
typedef std::vector<std::shared_ptr<Migration>> IdleRanges;

class Process
{
  public:
    int load(pid_t n);
    int split_ranges(unsigned long max_bytes);
    IdleRanges& get_ranges() { return idle_ranges; }

  private:
    void add_range(unsigned long start, unsigned long end);

  public:
    pid_t      pid;
    ProcStatus proc_status;
    ProcMaps   proc_maps;
    IdleRanges idle_ranges;
};

typedef std::unordered_map<pid_t, std::shared_ptr<Process>> ProcessHash;

class ProcessCollection
{
  public:
    int collect();
    int collect(PolicySet& policies);
    ProcessHash& get_proccesses() { return proccess_hash; }
    void dump();

  private:
    static const unsigned long SPLIT_RANGE_SIZE = (1<<30);
    int filter_by_policy(std::shared_ptr<Process> &process,
                         Policy &policy);


  private:
    ProcPid pids;
    ProcessHash proccess_hash;
};

#endif
// vim:set ts=2 sw=2 et:
