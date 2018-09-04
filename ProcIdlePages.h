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
};

class ProcIdlePages
{
  public:
    ProcIdlePages(): pid(0) {};
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

  private:
    static const unsigned long PTE_SIZE = 1UL << 12;
    static const unsigned long PMD_SIZE = 1UL << 21;
    static const unsigned long PUD_SIZE = 1UL << 30;
    static const unsigned long P4D_SIZE = 1UL << 39;

    pid_t pid;
    ProcMaps proc_maps;
    int nr_walks;

    // VA => refs
    // accumulated by walk()
    std::unordered_map<unsigned long, unsigned char> page_refs_4k;
    std::unordered_map<unsigned long, unsigned char> page_refs_2m;

    // refs => page count
    // accumulated by count_refs()
    std::vector<unsigned long> refs_count_4k;
    std::vector<unsigned long> refs_count_2m;
};

#endif
// vim:set ts=2 sw=2 et:
