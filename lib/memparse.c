/*
 * memparse() borrowed from linux/lib/cmdline.c
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 *
 */

#include <stdlib.h>

/**
 *	memparse - parse a string with mem suffixes into a number
 *	@ptr: Where parse begins
 *	@retptr: (output) Optional pointer to next char after parse completes
 *
 *	Parses a string into a number.  The number stored at @ptr is
 *	potentially suffixed with K, M, G, T, P, E.
 */

unsigned long long memparse(const char *ptr, char **retptr)
{
	char *endptr;	/* local pointer to end of parsed string */

	unsigned long long ret = strtoull(ptr, &endptr, 0);

	switch (*endptr) {
	case 'E':
	case 'e':
		ret <<= 10;
	case 'P':
	case 'p':
		ret <<= 10;
	case 'T':
	case 't':
		ret <<= 10;
	case 'G':
	case 'g':
		ret <<= 10;
	case 'M':
	case 'm':
		ret <<= 10;
	case 'K':
	case 'k':
		ret <<= 10;
		endptr++;
	default:
		break;
	}

	if (retptr)
		*retptr = endptr;

	return ret;
}
