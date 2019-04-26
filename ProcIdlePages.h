/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 *          Yao Yuan <yuan.yao@intel.com>
 *          Liu Jingqi <jingqi.liu@intel.com>
 */

#ifndef AEP_PROC_IDLE_PAGES_H
#define AEP_PROC_IDLE_PAGES_H

// interface to /proc/PID/idle_pages

#include <string>
#include <sys/user.h>
#include <sys/types.h>
#include <unordered_map>
#include "ProcMaps.h"
#include "AddrSequence.h"
#include "Option.h"

static const unsigned long PTE_SIZE = 1UL << 12;
static const unsigned long PMD_SIZE = 1UL << 21;
static const unsigned long PUD_SIZE = 1UL << 30;
static const unsigned long P4D_SIZE = 1UL << 39;
static const unsigned long PGDIR_SIZE = 1UL << 39;
static const unsigned long TASK_SIZE_MAX = (1UL << 47) - PAGE_SIZE;

#define SCAN_HUGE_PAGE      O_NONBLOCK
#define SCAN_SKIM_IDLE      O_NOFOLLOW

enum ProcIdlePageType
{
  PTE_ACCESSED,	/* 4k page */
  PMD_ACCESSED,	/* 2M page */
  PUD_PRESENT,	/* 1G page */
  MAX_ACCESSED = PUD_PRESENT,

  PTE_DIRTY,    /* 3 */
  PMD_DIRTY,

  PTE_IDLE,     /* 5 */
  PMD_IDLE,
  PMD_IDLE_PTES,	/* all PTE idle */

  PTE_HOLE,     /* 8 */
  PMD_HOLE,

  PIP_CMD,      /* 10 */

  IDLE_PAGE_TYPE_MAX
};

enum ref_location {
  MAX_NID = 31,
  REF_LOC_UNKNOWN,
  REF_LOC_DRAM,
  REF_LOC_PMEM,
  REF_LOC_ALL,
  REF_LOC_MAX,
};

#define PIP_TYPE(a)             (0xf & (a >> 4))
#define PIP_SIZE(a)	            (0xf & a)
#define PIP_COMPOSE(type, nr)	  ((type << 4) | nr)

#define PIP_CMD_SET_HVA		      PIP_COMPOSE(PIP_CMD, 0)

#define EPT_IDLE_BUF_MIN        ((sizeof(uint64_t) + 2) * 2)

extern unsigned long pagetype_size[IDLE_PAGE_TYPE_MAX];
extern const char* pagetype_name[IDLE_PAGE_TYPE_MAX];
extern int pagetype_shift[IDLE_PAGE_TYPE_MAX];
extern int pagetype_index[];
extern int pagetype_batchsize[IDLE_PAGE_TYPE_MAX];

typedef std::unordered_map<unsigned long, uint8_t> page_refs_map;
typedef std::vector<unsigned long>  histogram_type;
typedef histogram_type  histogram_2d_type[REF_LOC_MAX];


struct ProcIdleRefs
{
  // VA => refs
  // accumulated by walk()
  AddrSequence page_refs;

  // refs => page count
  // accumulated by count_refs()
  histogram_2d_type histogram_2d;
};

class ProcIdlePages
{
  public:
    ProcIdlePages();

    void set_pid(pid_t i) { pid = i; }
    pid_t get_pid() { return pid; }

    void set_va_range(unsigned long start, unsigned long end);
    void set_policy(Policy &pol);

    int walk();
    int has_io_error() const { return io_error; }

    ProcIdleRefs& get_pagetype_refs(ProcIdlePageType type)
                   { return pagetype_refs[pagetype_index[type]]; }

    int get_nr_walks() { return nr_walks; }

  private:
    int walk_vma(proc_maps_entry& vma);

    int open_file(void);

    uint64_t u8_to_u64(uint8_t a[]);
    void parse_idlepages(proc_maps_entry& vma,
                         unsigned long& va,
                         unsigned long end,
                         int bytes);
    void dump_idlepages(proc_maps_entry& vma, int bytes);

    void inc_page_refs(ProcIdlePageType type, int nr,
                       unsigned long va, unsigned long end);

    unsigned long va_to_offset(unsigned long va);
    unsigned long offset_to_va(unsigned long offset);

  protected:
    pid_t pid;
    Policy policy;

    ProcMaps proc_maps;
    unsigned long va_start;
    unsigned long va_end;

    // if negative, indicates exited process;
    // if positive, indicates skipped process
    int io_error;

  protected:
    int nr_walks;
    ProcIdleRefs pagetype_refs[MAX_ACCESSED + 1];

  private:
    static const int READ_BUF_SIZE = 1 << 20;

    int idle_fd;
    std::vector<uint8_t> read_buf;

    unsigned long min_read_size;
    unsigned long next_va;
};

#endif
// vim:set ts=2 sw=2 et:
