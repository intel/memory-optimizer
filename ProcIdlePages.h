#ifndef AEP_PROC_IDLE_PAGES_H
#define AEP_PROC_IDLE_PAGES_H

// interface to /proc/PID/idle_pages

#include <string>
#include <sys/types.h>
#include <unordered_map>
#include "Option.h"
#include "ProcMaps.h"
#include "AddrSequence.h"

static const unsigned long PTE_SIZE = 1UL << 12;
static const unsigned long PMD_SIZE = 1UL << 21;
static const unsigned long PUD_SIZE = 1UL << 30;
static const unsigned long P4D_SIZE = 1UL << 39;
static const unsigned long PGDIR_SIZE = 1UL << 39;
static const unsigned long PAGE_SHIFT = 12;
static const unsigned long PAGE_SIZE = 1UL << PAGE_SHIFT;
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

typedef std::unordered_map<unsigned long, uint8_t> page_refs_map;

struct ProcIdleRefs
{
  // VA => refs
  // accumulated by walk()
  //page_refs_map page_refs;
  AddrSequence page_refs2;

  // refs => page count
  // accumulated by count_refs()
  //std::vector<unsigned long> refs_count;
  std::vector<unsigned long> refs_count2;
};

class ProcIdlePages
{
  public:
    ProcIdlePages(const Option& o): pid(0), option(o) {};
    ~ProcIdlePages() {};

    void set_pid(pid_t i) { pid = i; }
    pid_t get_pid() { return pid; }

    int walk_multi(int nr, float interval);
    void count_refs();
    int save_counts(std::string filename);

    ProcIdleRefs& get_pagetype_refs(ProcIdlePageType type)
                   { return pagetype_refs[type | PAGE_ACCESSED_MASK]; }

    int get_nr_walks() { return nr_walks; }

  private:
    bool should_stop();
    int walk();
    int walk_vma(proc_maps_entry& vma);
    void count_refs_one(ProcIdleRefs& prc);

    int open_file(void);

    void parse_idlepages(proc_maps_entry& vma,
                         unsigned long& va,
                         int bytes);

    void inc_page_refs(ProcIdlePageType type, int nr,
                       unsigned long va, unsigned long end);

    unsigned long va_to_offset(unsigned long start_va);
    unsigned long offset_to_va(unsigned long start_va);

  private:
    static const int READ_BUF_SIZE = PAGE_SIZE * 8;

    pid_t pid;
    const Option& option;

    ProcMaps proc_maps;
    int nr_walks;

    ProcIdleRefs pagetype_refs[MAX_ACCESSED + 1];

    int idle_fd;
    std::vector<ProcIdleExtent> read_buf;
};

#endif
// vim:set ts=2 sw=2 et:
