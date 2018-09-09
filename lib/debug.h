#ifndef AEP_DEBUG_H
#define AEP_DEBUG_H

#define printd(fmt, args...)	verbose_printf(1, fmt, ##args)
#define printdd(fmt, args...)	verbose_printf(2, fmt, ##args)

extern int debug_level(void);
extern int verbose_printf(int level, const char *format, ...);

#endif
