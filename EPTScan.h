/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 */

#ifndef AEP_EPT_SCAN_H
#define AEP_EPT_SCAN_H

#include <string>

#include "ProcIdlePages.h"
#include "Numa.h"
#include "AddrSequence.h"
#include "MovePages.h"

class EPTScan: public ProcIdlePages
{
  public:
    void prepare_walks(int max_walks);
    int walk_multi(int nr, float interval);
    void gather_walk_stats(unsigned long& young_bytes,
                           unsigned long& top_bytes,
                           unsigned long& all_bytes);

    static void reset_sys_refs_count(int nr_walks);

    void count_refs();
    static int save_counts(std::string filename);

    void set_numacollection(NumaNodeCollection* new_numa_collection) {
      numa_collection = new_numa_collection;
    }

    int get_memory_type();
    int get_memory_type_range(void** addrs, unsigned long count,
                              AddrSequence& addrobj);

  private:
    bool should_stop();
    void count_refs_one(ProcIdleRefs& prc);

  private:
    static std::vector<unsigned long> sys_refs_count[MAX_ACCESSED + 1];

  protected:
     NumaNodeCollection* numa_collection = NULL;

};

#endif
// vim:set ts=2 sw=2 et:
