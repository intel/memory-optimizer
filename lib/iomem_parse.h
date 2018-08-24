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

struct memory_range {
	unsigned long long start, end;
	unsigned type;

#define RANGE_RAM		0
#define RANGE_RESERVED		1
#define RANGE_ACPI		2
#define RANGE_ACPI_NVS		3
#define RANGE_UNCACHED		4
#define RANGE_PMEM		6
#define RANGE_PRAM		11
};
