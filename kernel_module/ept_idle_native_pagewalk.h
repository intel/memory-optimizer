#ifndef _EPT_IDLE_NATIVE_PAGEWALK_H
#define _EPT_IDLE_NATIVE_PAGEWALK_H

int ept_idle_walk_page_range(unsigned long start, unsigned long end,
                             struct mm_walk *walk);

#endif
