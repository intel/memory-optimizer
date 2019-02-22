/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
 */

#include <cerrno>

#include <time.h>
#include <unistd.h>

#include "hmd-common.h"

unsigned long rdclock(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

unsigned long rdclock_diff(unsigned long prev, unsigned long cur)
{
  return cur - prev;
}

int read_all(int fd, void *buf, size_t n)
{
  int left = n;

  while (left) {
    int ret = read(fd, buf, left);

    if (ret < 0 && errno == EINTR)
      continue;
    if (ret <= 0)
      return ret;

    left -= ret;
    buf  = static_cast<char *>(buf) + ret;
  }

  return n;
}
