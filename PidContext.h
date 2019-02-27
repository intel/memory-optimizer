/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Yuan Yao <yuan.yao@intel.com>
 *
 */

#ifndef _PIDCONTEXT_H_
#define _PIDCONTEXT_H_

#include <sys/user.h>
#include <sys/types.h>

#include <atomic>

class PidContext
{
  public:
    void add_dram_quota(long value)
    { dram_quota += value; }

    void sub_dram_quota(long value)
    { dram_quota -= value; }

    long get_dram_quota(void)
    { return dram_quota.load(std::memory_order_acquire); }

    void set_dram_quota(long value)
    { dram_quota.store(value, std::memory_order_release); }

    void set_pid(pid_t value)
    { pid = value; }

    pid_t get_pid(void)
    { return pid; }

  private:
    std::atomic_long dram_quota = {0};
    pid_t pid = -1;
};



#endif
