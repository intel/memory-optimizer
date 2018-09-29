#ifndef AEP_ADDR_SEQUENCE_H
#define AEP_ADDR_SEQUENCE_H

#include <map>
#include <vector>
#include <memory>

struct DeltaPayload
{
  uint8_t delta;    // in pagesize unit
  uint8_t payload;  // stores refs count
}__attribute__((packed));

struct AddrCluster
{
  unsigned long start;
  int size;
  DeltaPayload *deltas; // points into AddrSequence::bufs
};

// A sparse array for ordered addresses.
// There will be 2 such arrays, one for 4K pages, another or 2M pages.
//
// Since addresses will have good spacial locality, organize them by clusters.
// Each AddrCluster represents a range of addresses that are close enough to
// each other, within 255 pages distance from prev/next neighbors, thus can be
// encoded by 8bit delta to save space.
//
// The addresses will be appended, updated or visited all sequentially.
class AddrSequence
{
  public:
    enum error {
        CREATE_CLUSTER_FAILED  = 100,
        ADDR_NOT_FOUND,
        IGNORE_DUPLICATED_ADDR,
        END_OF_SEQUENCE,
    };

  public:
    AddrSequence();
    ~AddrSequence();
    size_t size() const { return addr_size; }
    bool empty() const  { return 0 == addr_size; }
    unsigned long get_top_bytes() const { return top_bytes; }
    int get_pageshift() const { return pageshift; }

    void set_pageshift(int shift);
    void clear();

    // call me before starting each walk
    int rewind();

    // n=0: [addr]=0 if not already set
    // n=1: [addr]=1 if not already set, [addr]++ if already there
    //
    // Depending on nr_walks, it'll behave differently
    // - appending: for the first walk
    // will add addresses and set payload to 0 or 1
    // will allocate bufs
    //
    // - updating: for (nr_walks >= 2) walks
    // will do ++payload
    // will ignore addresses not already there
    int inc_payload(unsigned long addr, int n);
    int smooth_payloads();

    // for sequential visiting
    bool prepare_get();
    int get_first(unsigned long& addr, uint8_t& payload);
    int get_next(unsigned long& addr, uint8_t& payload);

#ifdef ADDR_SEQ_SELF_TEST
    int self_test();
    int do_self_test(unsigned long pagesize, int max_loop, bool is_pref);
    int do_self_test_walk(unsigned long pagesize, bool is_perf);
    int do_self_test_compare(unsigned long pagesize, bool is_perf);
#endif

  private:
    struct walk_iterator{
      unsigned long  cluster_iter;
      unsigned long  cluster_iter_end;
      unsigned long  delta_sum;
      int            delta_index;

      AddrCluster*   cur_cluster_ptr;
      DeltaPayload*  cur_delta_ptr;
    };

    int append_addr(unsigned long addr, int n);
    int update_addr(unsigned long addr, int n);

    int create_cluster(unsigned long addr, int n);
    AddrCluster new_cluster(unsigned long addr, void* buffer);
    int save_into_cluster(AddrCluster& cluster, unsigned long addr, int n);
    bool can_merge_into_cluster(AddrCluster& cluster, unsigned long addr);

    int get_free_buffer(void** free_ptr);
    int allocate_buf();
    void free_all_buf();
    int is_buffer_full() {
      return buf_used_count == MAX_ITEM_COUNT;
    }

    void reset_iterator(walk_iterator& iter, unsigned long new_start) {
      iter.cluster_iter = new_start;
      iter.cluster_iter_end = addr_clusters.size();
      iter.delta_sum = 0;
      iter.delta_index = 0;

      if (!addr_clusters.empty())
          do_walk_update_current_ptr(iter);
    }

    int in_append_period() { return nr_walks < 2; }

    int  do_walk(walk_iterator& iter, unsigned long& addr, uint8_t& payload);
    void do_walk_move_next(walk_iterator& iter);
    void do_walk_update_payload(walk_iterator& iter,
                                unsigned addr, uint8_t payload);
    bool do_walk_continue(int ret_val) {
      return ret_val >=0 && ret_val != END_OF_SEQUENCE;
    }

    void do_walk_update_current_ptr(walk_iterator& iter) {
        iter.cur_cluster_ptr = &addr_clusters[iter.cluster_iter];
        iter.cur_delta_ptr = iter.cur_cluster_ptr->deltas;
    }

  private:
    const static int BUF_SIZE = 0x10000; // 64KB;
    const static int ITEM_SIZE = sizeof(struct DeltaPayload);
    const static int MAX_ITEM_COUNT = BUF_SIZE / ITEM_SIZE;
    const static unsigned long MAX_DELTA_DIST = ( 1 << ( sizeof(uint8_t) * 8 ) ) - 1;

    int nr_walks;
    int pageshift;
    unsigned long pagesize;
    unsigned long addr_size;  // # of addrs stored
    unsigned long top_bytes;  // nr_top_pages * pagesize

    std::vector<AddrCluster>     addr_clusters;

    // inc_payload() will allocate new fixed size buf on demand,
    // avoiding internal/external fragmentations.
    // Only freed on clear().
    std::allocator<DeltaPayload> buf_allocator;
    std::vector<DeltaPayload*>   buf_pool;
    int buf_used_count;

    walk_iterator walk_iter;
    walk_iterator find_iter;

    unsigned long last_cluster_end;

#ifdef ADDR_SEQ_SELF_TEST
    std::map<unsigned long, uint8_t> test_map;
#endif
};

#endif
// vim:set ts=2 sw=2 et:
