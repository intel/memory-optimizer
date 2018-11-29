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

#define err(x) perror(x), exit(1)
#define mb() asm volatile("" ::: "memory")
#define MB (1024*1024)
typedef unsigned long long u64;
typedef long long s64;

#define MS_PER_SEC	1000ULL
#define NS_PER_SEC	1000000000ULL
#define NS_PER_MSEC	1000000ULL
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)

// remove because duplicated with /usr/include/x86_64-linux-gnu/sys/user.h:174:0:
// #define PAGE_MASK	(PAGE_SIZE - 1)

#endif /* __COMMON__HH__ */
