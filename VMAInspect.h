#ifndef _VMA_INSPECT_H
#define _VMA_INSPECT_H

#include <sys/types.h>
#include "MovePages.h"

struct proc_maps_entry;
class Formatter;
class NumaNodeCollection;

class VMAInspect
{
  public:
    VMAInspect() {};
    ~VMAInspect() {};

    int dump_task_nodes(pid_t i, Formatter* m);
    int dump_vma_nodes(proc_maps_entry& vma, MovePagesStatusCount& status_sum);
    void set_numa_collection(NumaNodeCollection* new_numa_collection) {
      numa_collection = new_numa_collection;
      locator.set_numacollection(new_numa_collection);
    }

  private:
    void fill_addrs(std::vector<void *>& addrs, unsigned long start);
    void dump_node_percent(int slot);

  private:
    pid_t pid;
    Formatter* fmt;
    MovePages locator;
    NumaNodeCollection* numa_collection;
};

#endif
// vim:set ts=2 sw=2 et:
