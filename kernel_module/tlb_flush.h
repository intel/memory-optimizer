#ifndef _TLB_FLUSH_H
#define _TLB_FLUSH_H

#include <asm/tlbflush.h>

void copied_flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
                               unsigned long end, unsigned int stride_shift,
                               bool freed_tables);

#endif
