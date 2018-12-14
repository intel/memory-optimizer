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

#ifndef __COMMON__HH__
#define __COMMON__HH__

#include <system_error>

#include <sys/user.h>
#include <errno.h>

#define MB (1024*1024)
typedef unsigned long long u64;
typedef long long s64;

#define MS_PER_SEC	1000ULL
#define NS_PER_SEC	1000000000ULL
#define NS_PER_MSEC	1000000ULL

inline void sys_err(const std::string& what)
{
  throw std::system_error(errno, std::generic_category(), what);
}

inline void mb()
{
  asm volatile("" ::: "memory");
}

#endif /* __COMMON__HH__ */
