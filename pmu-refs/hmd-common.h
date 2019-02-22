/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
 */

#ifndef __HMD_COMMON_HH__
#define __HMD_COMMON_HH__

#include "common.h"

unsigned long rdclock(void);
int read_all(int fd, void *buf, size_t n);

#endif /* __COMMON__HH__ */
