/*
 * page-types: Tool for querying page flags
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should find a copy of v2 of the GNU General Public License somewhere on
 * your Linux system; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * Copyright (C) 2009 Intel corporation
 *
 * Authors: Wu Fengguang <fengguang.wu@intel.com>
 */

#include <inttypes.h>
#include <linux/kernel-page-flags.h>
#include <stdio.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

#ifndef KPF_PGTABLE
#define KPF_PGTABLE             26
#endif

/* kernel hacking assistances
 * WARNING: subject to change, never rely on them!
 */
#define KPF_RESERVED		32
#define KPF_MLOCKED		33
#define KPF_MAPPEDTODISK	34
#define KPF_PRIVATE		35
#define KPF_PRIVATE_2		36
#define KPF_OWNER_PRIVATE	37
#define KPF_ARCH		38
#define KPF_UNCACHED		39
#define KPF_SOFTDIRTY		40

static const char * const page_flag_names[] = {
	[KPF_LOCKED]		= "L:locked",
	[KPF_ERROR]		= "E:error",
	[KPF_REFERENCED]	= "R:referenced",
	[KPF_UPTODATE]		= "U:uptodate",
	[KPF_DIRTY]		= "D:dirty",
	[KPF_LRU]		= "l:lru",
	[KPF_ACTIVE]		= "A:active",
	[KPF_SLAB]		= "S:slab",
	[KPF_WRITEBACK]		= "W:writeback",
	[KPF_RECLAIM]		= "I:reclaim",
	[KPF_BUDDY]		= "B:buddy",

	[KPF_MMAP]		= "M:mmap",
	[KPF_ANON]		= "a:anonymous",
	[KPF_SWAPCACHE]		= "s:swapcache",
	[KPF_SWAPBACKED]	= "b:swapbacked",
	[KPF_COMPOUND_HEAD]	= "H:compound_head",
	[KPF_COMPOUND_TAIL]	= "T:compound_tail",
	[KPF_HUGE]		= "G:huge",
	[KPF_UNEVICTABLE]	= "u:unevictable",
	[KPF_HWPOISON]		= "X:hwpoison",
	[KPF_NOPAGE]		= "n:nopage",
	[KPF_KSM]		= "x:ksm",
	[KPF_THP]		= "t:thp",
	[KPF_BALLOON]		= "o:balloon",
	[KPF_PGTABLE]		= "g:pgtable",
	[KPF_ZERO_PAGE]		= "z:zero_page",
	[KPF_IDLE]              = "i:idle_page",

	[KPF_RESERVED]		= "r:reserved",
	[KPF_MLOCKED]		= "m:mlocked",
	[KPF_MAPPEDTODISK]	= "d:mappedtodisk",
	[KPF_PRIVATE]		= "P:private",
	[KPF_PRIVATE_2]		= "p:private_2",
	[KPF_OWNER_PRIVATE]	= "O:owner_private",
	[KPF_ARCH]		= "h:arch",
	[KPF_UNCACHED]		= "c:uncached",
	[KPF_SOFTDIRTY]		= "f:softdirty",

};


char *page_flag_name(uint64_t flags)
{
	static char buf[65];
	int present;
	size_t i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(page_flag_names); i++) {
		present = (flags >> i) & 1;
		if (!page_flag_names[i]) {
			/* if (present) */
				/* fatal("unknown flag bit %d\n", i); */
			continue;
		}
		buf[j++] = present ? page_flag_names[i][0] : '_';
	}

	return buf;
}

char *page_flag_longname(uint64_t flags)
{
	static char buf[1024];
	size_t i, n;

	for (i = 0, n = 0; i < ARRAY_SIZE(page_flag_names); i++) {
		if (!page_flag_names[i])
			continue;
		if ((flags >> i) & 1)
			n += snprintf(buf + n, sizeof(buf) - n, "%s,",
					page_flag_names[i] + 2);
	}
	if (n)
		n--;
	buf[n] = '\0';

	return buf;
}

