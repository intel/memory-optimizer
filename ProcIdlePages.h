#ifndef AEP_PROC_IDLE_PAGES_H
#define AEP_PROC_IDLE_PAGES_H

// interface to /proc/PID/idle_pages

#include <string>
#include <sys/types.h>
#include <unordered_map>
#include "ProcMaps.h"

static const unsigned long PTE_SIZE = 1UL << 12;
static const unsigned long PMD_SIZE = 1UL << 21;
static const unsigned long PUD_SIZE = 1UL << 30;
static const unsigned long P4D_SIZE = 1UL << 39;
static const unsigned long PGDIR_SIZE = 1UL << 39;
static const unsigned long PAGE_SIZE = 4096;

enum ProcIdlePageType
{
  // 4k page
  PTE_HOLE,
  PTE_IDLE,
  PTE_ACCESSED,

  // 2M page
  PMD_HOLE,
  PMD_IDLE,
  PMD_ACCESSED,

  // 1G page
  PUD_HOLE,
  PUD_IDLE,
  PUD_ACCESSED,

  // 512G
  P4D_HOLE,
  PGDIR_HOLE,

  IDLE_PAGE_TYPE_MAX
};

struct ProcIdleExtent
{
  unsigned type : 4;  // ProcIdlePageType
  unsigned nr   : 4;
}__attribute__((packed));


enum PageLevel
{
    BEGIN  = 0,
    PAGE_4K,
    PAGE_2M,
    PAGE_1G,

    END,
};

typedef std::unordered_map<unsigned long, unsigned char> page_refs_map;

class ProcIdlePages
{
  public:
    ProcIdlePages(): pid(0) {};
    ~ProcIdlePages() {};

    void set_pid(pid_t i) { pid = i; }

    int walk_multi(int nr, float interval);
    int count_refs();
    int save_counts(std::string filename);

    const page_refs_map& get_page_refs(PageLevel level);

  private:
    int walk();
    int walk_vma(proc_maps_entry& vma);
    int count_refs_one(
                   page_refs_map& page_refs,
                   std::vector<unsigned long>& refs_count);

    int open_file(void);

    void parse_idlepages(proc_maps_entry& vma,
                         unsigned long& va,
                         int bytes);

    void inc_page_refs(page_refs_map& page_refs,
                       unsigned long va,
                       unsigned long page_size,
                       unsigned long count);

    unsigned long va_to_offset(unsigned long start_va);
    unsigned long offset_to_va(unsigned long start_va);

  private:
    static const int READ_BUF_SIZE = PAGE_SIZE * 8;

    pid_t pid;
    ProcMaps proc_maps;
    int nr_walks;

    // VA => refs
    // accumulated by walk()

    std::unordered_map<unsigned long, unsigned char> page_refs_4k;
    std::unordered_map<unsigned long, unsigned char> page_refs_2m;
    std::unordered_map<unsigned long, unsigned char> page_refs_1g;

    //leave empty to as error case, because do NOT want to throw() something.
    std::unordered_map<unsigned long, unsigned char> page_refs_unknow;
    // refs => page count
    // accumulated by count_refs()
    std::vector<unsigned long> refs_count_4k;
    std::vector<unsigned long> refs_count_2m;

    int idle_fd;
    std::vector<ProcIdleExtent> read_buf;
};

#endif
// vim:set ts=2 sw=2 et:
