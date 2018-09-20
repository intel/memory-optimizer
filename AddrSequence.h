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
    AddrSequence();
    ~AddrSequence();
    size_t size() const { return addr_size; }
    bool empty() const  { return 0 == addr_size; }

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
    int self_test_walk();
    int self_test_compare();
#endif

  private:
    int append_addr(unsigned long addr, int n);

    int update_addr(unsigned long addr, int n);
    
    int create_cluster(unsigned long addr, int n);

    AddrCluster new_cluster(unsigned long addr, void* buffer);

    void* get_free_buffer();

    int save_into_cluster(AddrCluster& cluster, unsigned long addr, int n);

    bool can_merge_into_cluster(AddrCluster& cluster, unsigned long addr);

    int allocate_buf();
    void free_all_buf();
    int is_buffer_full() {
      return buf_used_count == MAX_ITEM_COUNT;
    }
    
    void cluster_change_for_update() {
        delta_update_sum = 0;
        delta_update_index = 0;
    }

    void cluster_added_for_update(std::map<unsigned long, AddrCluster>::iterator& new_start) {
        //move iter_update to last one, because we grow at end
        iter_update = new_start;
        delta_update_sum = 0;
        delta_update_index = 0;
    }

    int in_append_period() {
      return nr_walks < 2;
    }

  private:        
    const static int BUF_SIZE = 0x10000; // 64KB;
    const static int ITEM_SIZE = sizeof(struct DeltaPayload);
    const static int MAX_ITEM_COUNT = BUF_SIZE / ITEM_SIZE;

    
    int nr_walks;
    int pageshift;
    unsigned long  pagesize;
    unsigned long addr_size;  // # of addrs stored

    std::map<unsigned long, AddrCluster> addr_clusters;

    // inc_payload() will allocate new fixed size buf on demand,
    // avoiding internal/external fragmentations.
    // Only freed on clear().
    //std::vector<std::array<uint8_t, BUF_SIZE>> bufs;
    
    typedef uint8_t delta_buf[BUF_SIZE];
    std::allocator<delta_buf> buf_allocator;
    std::vector<delta_buf*>   buf_pool;
    int buf_used_count;

    std::map<unsigned long, AddrCluster>::iterator iter_cluster;
    int iter_delta_index;
    int iter_delta_val;

#ifdef ADDR_SEQ_SELF_TEST
    std::map<unsigned long, uint8_t> test_map;
#endif

    std::map<unsigned long, AddrCluster>::iterator iter_update;
    int delta_update_sum;
    int delta_update_index;
    unsigned long last_cluster_end;

};

#endif
// vim:set ts=2 sw=2 et:
