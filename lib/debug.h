/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 */

#ifndef AEP_DEBUG_H
#define AEP_DEBUG_H

#define printd(fmt, args...)	verbose_printf(1, fmt, ##args)
#define printdd(fmt, args...)	verbose_printf(2, fmt, ##args)
#define err(x) perror(x), exit(1)

extern int debug_level(void);
extern int verbose_printf(int level, const char *format, ...);

#endif
