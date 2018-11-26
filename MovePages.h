#ifndef _MOVE_PAGES_H
#define _MOVE_PAGES_H

#include <unordered_map>
#include <string>
#include <vector>

class BandwidthLimit;
class Formatter;

#define MPOL_MF_SW_YOUNG (1<<7)

typedef std::unordered_map<int, int> MovePagesStatusCount;

struct MoveStats
{
    unsigned long to_move_kb;
    unsigned long skip_kb;
    unsigned long move_kb;

    void clear();
    void account(MovePagesStatusCount& status_count, int page_shift, int target_node);
};

class MovePages
{
  public:
    MovePages();
    ~MovePages() {};

    void set_pid(pid_t i)                     { pid = i; }
    void set_target_node(int nid)             { target_node = nid; }
    void set_page_shift(int t)                { page_shift = t; }
    void set_batch_size(unsigned long npages) { batch_size = npages; }
    void set_flags(int f)                     { flags = f; }
    void set_throttler(BandwidthLimit* new_throttler) {throttler = new_throttler;}

    long move_pages(std::vector<void *>& addrs);
    long move_pages(void **addrs, unsigned long count);
    long locate_move_pages(std::vector<void *>& addrs, MoveStats *stats);

    std::vector<int>& get_status()            { return status; }
    MovePagesStatusCount& get_status_count()  { return status_count; }
    void clear_status_count()                 { status_count.clear(); }
    void calc_status_count();
    void add_status_count(MovePagesStatusCount& status_sum);
    void show_status_count(Formatter* fmt);
    void show_status_count(Formatter* fmt, MovePagesStatusCount& status_sum);
    void account_stats(MoveStats *stats);

  private:
    pid_t pid;
    int flags;
    int target_node;

    unsigned long page_shift;
    unsigned long batch_size; // used by locate_move_pages()

    // Get the status after migration
    std::vector<int> status;

    MovePagesStatusCount status_count;

    BandwidthLimit* throttler;
};

#endif
// vim:set ts=2 sw=2 et:
