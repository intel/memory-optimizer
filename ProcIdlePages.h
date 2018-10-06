#ifndef AEP_PROC_IDLE_PAGES_H
#define AEP_PROC_IDLE_PAGES_H

// interface to /proc/PID/idle_pages

#include <string>
#include <sys/types.h>
#include <unordered_map>
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

    int walk_multi(int nr, float interval);
    void prepare_walks(int max_walks);
    int walk();

    static void reset_sys_refs_count();
    void count_refs();
    static int save_counts(std::string filename);

    ProcIdleRefs& get_pagetype_refs(ProcIdlePageType type)
                   { return pagetype_refs[type | PAGE_ACCESSED_MASK]; }

    int get_nr_walks() { return nr_walks; }
    void get_sum(unsigned long& top_bytes, unsigned long& all_bytes);

  private:
    bool should_stop();
    int walk_vma(proc_maps_entry& vma);
    void count_refs_one(ProcIdleRefs& prc);

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

    ProcMaps proc_maps;
    unsigned long va_start;
    unsigned long va_end;

  private:
    static const int READ_BUF_SIZE = PAGE_SIZE * 8;
    static std::vector<unsigned long> sys_refs_count[MAX_ACCESSED + 1];

    int nr_walks;
    ProcIdleRefs pagetype_refs[MAX_ACCESSED + 1];

    int idle_fd;
    std::vector<ProcIdleExtent> read_buf;
};

#endif
// vim:set ts=2 sw=2 et:
