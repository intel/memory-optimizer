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
    size_t size() { return addr_size; }
    bool empty()  { return addr_size > 0; }

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

    // for sequential visiting
    int get_first(unsigned long& addr, uint8_t& payload);
    int get_next(unsigned long& addr, uint8_t& payload);

    // private:
    int append_addr(unsigned long addr, int n);

    int update_addr(unsigned long addr, int n);
    
    int create_cluster(unsigned long addr, int n);

    AddrCluster new_cluster(unsigned long addr, void* buffer);

    void* get_free_buffer();

    int save_into_cluster(AddrCluster& cluster, unsigned long addr, int n);

    int can_merge_into_cluster(AddrCluster& cluster, unsigned long addr);

    DeltaPayload* addr_to_delta_ptr(AddrCluster& cluster, unsigned long addr);
    
    DeltaPayload* raw_buffer_ptr() {
      uint8_t* ptr = (uint8_t*)bufs_ptr_recorder.back();
      return (DeltaPayload*)(ptr + buf_item_used * BUF_ITEM_SIZE);
    }
    
    int is_buffer_full() {
      return buf_item_used == BUF_ITEM_COUNT;
    }

    unsigned long  addr_to_delta(AddrCluster& cluster, unsigned long addr) {
        unsigned long addr_delta = addr - cluster_end(cluster);
        return addr_delta >> pageshift;
    }

    unsigned long cluster_end(AddrCluster& cluster) {
        int delta_val= 0;
        
        for (int i = 0; i < cluster.size; ++i) {
            delta_val += cluster.deltas[i].delta;
        }
        
        return cluster.start + delta_val * pagesize;
    }

    int allocate_buf(int count);

    void free_all_buf();
    
  private:        
    const static int BUF_SIZE = 0x10000; // 64KB;
    const static int BUF_ITEM_SIZE = sizeof(struct DeltaPayload);
    const static int BUF_ITEM_COUNT = BUF_SIZE / BUF_ITEM_SIZE;
    typedef uint8_t buf_type[BUF_SIZE];
    
    int nr_walks;
    int pageshift;
    unsigned long  pagesize;

    unsigned long addr_size;  // # of addrs stored

    std::map<unsigned long, AddrCluster> addr_clusters;

    // inc_payload() will allocate new fixed size buf on demand,
    // avoiding internal/external fragmentations.
    // Only freed on clear().
    //std::vector<std::array<uint8_t, BUF_SIZE>> bufs;
    
    std::allocator<buf_type> bufs;
    std::vector<buf_type*>   bufs_ptr_recorder;
    
    int buf_item_used;

    std::map<unsigned long, AddrCluster>::iterator iter_cluster;
    int iter_delta_index;
    int iter_delta_val;
};

#endif
// vim:set ts=2 sw=2 et:
