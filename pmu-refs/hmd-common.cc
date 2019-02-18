/*
 * Copyright (c) 2018 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
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
