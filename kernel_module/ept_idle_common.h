// SPDX-License-Identifier: GPL-2.0
#ifndef _EPT_IDLE_COMMON_H
#define _EPT_IDLE_COMMON_H

/* Fix leak of 5 level paging supporting on old kernel*/
#ifndef CONFIG_PGTABLE_LEVELS
  #define EPT_IDLE_5_LEVEL_PGTABLE_SUPPORT
#else
  #if CONFIG_PGTABLE_LEVELS < 4
    #define EPT_IDLE_5_LEVEL_PGTABLE_SUPPORT
  #endif // #if CONFIG_PGTABLE_LEVELS < 4
#endif // #ifndef CONFIG_PGTABLE_LEVELS

#ifdef EPT_IDLE_5_LEVEL_PGTABLE_SUPPORT

#define p4d_t                    pgd_t
#define p4d_flags                pgd_flags
#define p4d_offset(pgd, start)   (pgd)
#define p4d_addr_end(addr, end)  (end)
#define p4d_present(p4d)         1
#define p4d_ERROR(p4d)           do { } while(0)
#define p4d_clear                pgd_clear
#define p4d_none(p4d)            0
#define p4d_bad(p4d)             0
#define p4d_clear_bad            pgd_clear_bad
#endif

#ifndef pgd_offset_pgd
#define pgd_offset_pgd(pgd, address) (pgd + pgd_index((address)))
#endif


#endif
