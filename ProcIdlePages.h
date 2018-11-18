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

enum ProcIdlePageType
{
  // 4k page
  PTE_IDLE,
  PTE_ACCESSED,

  PAGE_ACCESSED_MASK = PTE_ACCESSED,

  // 2M page
  PMD_IDLE,
  PMD_ACCESSED,

  // 1G page
  PUD_IDLE,
  PUD_ACCESSED,

  MAX_ACCESSED = PUD_ACCESSED,

  PTE_HOLE,
  PMD_HOLE,
  PUD_HOLE,
  P4D_HOLE,
  PGDIR_HOLE,

  IDLE_PAGE_TYPE_MAX
};

struct ProcIdleExtent
{
  unsigned type : 4;  // ProcIdlePageType
  unsigned nr   : 4;
}__attribute__((packed));


extern unsigned long pagetype_size[IDLE_PAGE_TYPE_MAX];
extern const char* pagetype_name[IDLE_PAGE_TYPE_MAX];
extern unsigned long pagetype_shift[IDLE_PAGE_TYPE_MAX];

typedef std::unordered_map<unsigned long, uint8_t> page_refs_map;

struct ProcIdleRefs
{
  // VA => refs
  // accumulated by walk()
  AddrSequence page_refs;

  // refs => page count
  // accumulated by count_refs()
  std::vector<unsigned long> refs_count;
};

class ProcIdlePages
{
  public:
    ProcIdlePages(pid_t n);
    ~ProcIdlePages() {};

    void set_pid(pid_t i) { pid = i; }
    pid_t get_pid() { return pid; }

    void set_va_range(unsigned long start, unsigned long end);
    void set_policy(Policy &pol);

    int walk();
    int has_io_error() const { return io_error; }

    ProcIdleRefs& get_pagetype_refs(ProcIdlePageType type)
                   { return pagetype_refs[type | PAGE_ACCESSED_MASK]; }

    int get_nr_walks() { return nr_walks; }

  private:
    int walk_vma(proc_maps_entry& vma);

    int open_file(void);

    void parse_idlepages(proc_maps_entry& vma,
                         unsigned long& va,
                         unsigned long end,
                         int bytes);

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
    static const int READ_BUF_SIZE = PAGE_SIZE * 32;

    int idle_fd;
    std::vector<ProcIdleExtent> read_buf;
};

#endif
// vim:set ts=2 sw=2 et:
