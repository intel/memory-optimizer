/*
 * borrowed from kexec-tools
 *
 * Copyright (C) 2003-2005  Eric Biederman (ebiederm@xmission.com)
 * Copyright (C) 2018       Fengguang Wu <fengguang.wu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License).
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "iomem_parse.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define MAX_LINE 160
#define MAX_MEMORY_RANGES 2048
static struct memory_range memory_range[MAX_MEMORY_RANGES];

/**
 * /proc/iomem parsing code.
 *
 * @param[out] range pointer that will be set to an array that holds the
 *             memory ranges
 * @param[out] ranges number of ranges valid in @p range
 *
 * @return 0 on success, any other value on failure.
 */
int get_memory_ranges_proc_iomem(struct memory_range **range, int *ranges)
{
	const char *iomem= "/proc/iomem";
	int memory_ranges = 0;
	char line[MAX_LINE];
	FILE *fp;

	fp = fopen(iomem, "r");
	if (!fp) {
		fprintf(stderr, "Cannot open %s: %s\n",
			iomem, strerror(errno));
		return -1;
	}

	while (fgets(line, sizeof(line), fp) != 0) {
		unsigned long long start, end;
		char *str;
		int type;
		int consumed;
		int count;
		if (memory_ranges >= MAX_MEMORY_RANGES)
			break;
		count = sscanf(line, "%llx-%llx : %n",
			&start, &end, &consumed);
		if (count != 2)
			continue;
		
		if (consumed >= MAX_LINE)
			return -1;

		str = line + consumed;

		/* printf("%016Lx-%016Lx : %s", start, end, str); */

		if (memcmp(str, "System RAM\n", 11) == 0) {
			type = RANGE_RAM;
		}
		else if (memcmp(str, "reserved\n", 9) == 0) {
			type = RANGE_RESERVED;
		}
		else if (memcmp(str, "ACPI Tables\n", 12) == 0) {
			type = RANGE_ACPI;
		}
		else if (memcmp(str, "ACPI Non-volatile Storage\n", 26) == 0) {
			type = RANGE_ACPI_NVS;
		}
		else if (memcmp(str, "Persistent Memory (legacy)\n", 27) == 0) {
			type = RANGE_PRAM;
		}
		else if (memcmp(str, "Persistent Memory\n", 18) == 0) {
			type = RANGE_PMEM;
		}
		else {
			continue;
		}
		memory_range[memory_ranges].start = start;
		memory_range[memory_ranges].end = end;
		memory_range[memory_ranges].type = type;

		/* printf("%016Lx-%016Lx : %x\n", start, end, type); */

		memory_ranges++;
	}
	fclose(fp);
	*range = memory_range;
	*ranges = memory_ranges;
	return 0;
}
