#ifndef _VMA_INSPECT_H
#define _VMA_INSPECT_H

#include <sys/types.h>
#include "MovePages.h"

struct proc_maps_entry;
class Formatter;

class VMAInspect
{
  public:
    VMAInspect() {};
    ~VMAInspect() {};

    int dump_task_nodes(pid_t i, Formatter* m);
    int dump_vma_nodes(proc_maps_entry& vma);

  private:
    void fill_addrs(std::vector<void *>& addrs, unsigned long start);
    void dump_node_percent(int slot);

  private:
    pid_t pid;
    Formatter* fmt;
    MovePages locator;
};

#endif
// vim:set ts=2 sw=2 et:
