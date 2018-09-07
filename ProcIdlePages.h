#ifndef AEP_PROC_IDLE_PAGES_H
#define AEP_PROC_IDLE_PAGES_H

// interface to /proc/PID/idle_pages

#include <string>
#include <sys/types.h>
#include <unordered_map>
#include "ProcMaps.h"

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
};

struct ProcIdleExtent
{
  unsigned type : 4;  // ProcIdlePageType
  unsigned nr   : 4;
}__attribute__((packed));

class ProcIdlePages
{
  private:
    typedef std::unordered_map<unsigned long, unsigned char> page_refs_info;
    
  public:
    ProcIdlePages(): pid(0), lp_procfile(NULL) {};
    ~ProcIdlePages() {};

    void set_pid(pid_t i) { pid = i; }

    int walk_multi(int nr, float interval);
    int count_refs();
    int save_counts(std::string filename);

  private:
    int walk();
    int count_refs_one(
                   std::unordered_map<unsigned long, unsigned char>& page_refs,
                   std::vector<unsigned long>& refs_count);
    
    int read_idlepages_begin(void);
    void read_idlepages_end(void);
    int read_idlepages(ProcIdleExtent* lp_idle_info,
                       unsigned long read_size, unsigned long& completed_size);
    
    void parse_idlepages(unsigned long start_va,
                         unsigned long expect_end_va,
                         ProcIdleExtent* lp_idle_info,
                         unsigned long size,
                         unsigned long& parsed_end);
    
    void update_idlepages_info(page_refs_info& info,
                               unsigned long va, unsigned long page_size,
                               unsigned long count);

    int seek_idlepages(unsigned long start_va);

    unsigned long va_to_offset(unsigned long start_va);
    unsigned long offset_to_va(unsigned long start_va);
    
  private:
    static const unsigned long PTE_SIZE = 1UL << 12;
    static const unsigned long PMD_SIZE = 1UL << 21;
    static const unsigned long PUD_SIZE = 1UL << 30;
    static const unsigned long P4D_SIZE = 1UL << 39;
    static const unsigned long KiB = 1024;
    static const unsigned int IDLE_BUFFER_COUNT = 1024;
    
    pid_t pid;
    ProcMaps proc_maps;
    int nr_walks;

    // VA => refs
    // accumulated by walk()

    std::unordered_map<unsigned long, unsigned char> page_refs_4k;
    std::unordered_map<unsigned long, unsigned char> page_refs_2m;
    std::unordered_map<unsigned long, unsigned char> page_refs_1g;
    
    // refs => page count
    // accumulated by count_refs()
    std::vector<unsigned long> refs_count_4k;
    std::vector<unsigned long> refs_count_2m;

    FILE* lp_procfile;

    ProcIdleExtent data_buffer[IDLE_BUFFER_COUNT];
};

#endif
// vim:set ts=2 sw=2 et:
