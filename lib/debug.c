#include <stdio.h>
#include <stdarg.h>
#include "debug.h"

int verbose_printf(int level, const char *format, ...)
{
	if (debug_level() < level)
		return 0;

	va_list args;
	va_start(args, format);
	int ret = vprintf(format, args);
	va_end(args);

	return ret;
}

