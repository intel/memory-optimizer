#include "AddrSequence.h"

AddrSequence::AddrSequence()
{
  addr_size = 0;
  nr_walks = 0;
  pageshift = 0;
  pagesize = 0;

  //set this to froce alloc buffer when add cluster
  buf_item_used = BUF_ITEM_COUNT;
}

AddrSequence::~AddrSequence()
{
  clear();
}

void AddrSequence::clear()
{
  addr_size = 0;
  nr_walks = 0;
  addr_clusters.clear();
  free_all_buf();

  buf_item_used = BUF_ITEM_COUNT;
}

void AddrSequence::set_pageshift(int shift)
{
  pageshift = shift;
  pagesize = 1 << shift;
}

int AddrSequence::rewind()
{
  ++nr_walks;

  return 0;
}

int AddrSequence::inc_payload(unsigned long addr, int n)
{
    int ret_value;
    
    if (nr_walks < 2) {
        ret_value = append_addr(addr, n);
    } else {
        ret_value = update_addr(addr, n);
    }

    return ret_value;
}


int AddrSequence::update_addr(unsigned long addr, int n)
{
    //find if the addr already in the cluster, return if not
    int ret_val = 0;
    DeltaPayload *delta_ptr;
    auto begin = addr_clusters.begin();
    auto end = addr_clusters.end();
    
    for (; begin != end; ++begin) {
        delta_ptr = addr_to_delta_ptr(begin->second, addr);
        if (delta_ptr) {
          if (n)
              ++delta_ptr->payload;
          break;
        }
    }
    
    return ret_val;
}

int AddrSequence::get_first(unsigned long& addr, uint8_t& payload)
{
    return 0;
}

int AddrSequence::get_next(unsigned long& addr, uint8_t& payload)
{
    return 0;
}

int AddrSequence::append_addr(unsigned long addr, int n)
{
    int ret_val;
    
    auto last = addr_clusters.rbegin();

    //we already have a cluster, let's check if we can
    //put addr into this cluster
    if (last != addr_clusters.rend()) {
        
        AddrCluster& cluster = last->second;

        //try put into the cluster or create new cluster
        if (addr >= cluster.start) {            
            if (can_merge_into_cluster(cluster, addr)) 
                ret_val = save_into_cluster(cluster, addr, n);
            else
                ret_val = create_cluster(addr, n);
        } else {
            //is this right? only first time scan now
            ret_val = update_addr(addr, n);
        }
        
    } else {
        // no cluster, let's create it.
        ret_val = create_cluster(addr, n);
    }

    return ret_val;
}


int AddrSequence::create_cluster(unsigned long addr, int n)
{
    void* new_buf_ptr;
    int ret_val;

    ret_val = -1;
    new_buf_ptr = get_free_buffer();
    if (new_buf_ptr)
    {
        std::pair<unsigned long, AddrCluster>
            new_item(addr, new_cluster(addr, new_buf_ptr));

        ret_val = -1;
        auto insert_ret = addr_clusters.insert(new_item);        
        if (insert_ret.second) {
            ret_val = save_into_cluster(insert_ret.first->second,
                                        addr, n);

            return ret_val;
        }
    }
    
    return ret_val;
}


AddrCluster AddrSequence::new_cluster(unsigned long addr, void* buffer)
{
    AddrCluster new_item;

    new_item.start = addr;
    new_item.size = 0;
    new_item.deltas = (DeltaPayload*)buffer;

    return new_item;
}


void* AddrSequence::get_free_buffer()
{
    if (is_buffer_full()) {
        //todo: try catch exception here for fail
        if (allocate_buf(1))
            return NULL;

        buf_item_used = 0;
    }
    
    return raw_buffer_ptr();
}


int AddrSequence::save_into_cluster(AddrCluster& cluster,
                                    unsigned long addr, int n)
{
    uint8_t delta = addr_to_delta(cluster, addr);
    
    int index = cluster.size;
    
    cluster.deltas[index].delta = delta;
    cluster.deltas[index].payload = n;

    ++cluster.size;
    ++buf_item_used;
    ++addr_size;
    
    return 0;
}

int AddrSequence::can_merge_into_cluster(AddrCluster& cluster, unsigned long addr)
{
    int page_count = addr_to_delta(cluster, addr);

    if (page_count > 255
        || is_buffer_full())
        return false;

    return true;
}



DeltaPayload* AddrSequence::addr_to_delta_ptr(AddrCluster& cluster,
                                              unsigned long addr)
{
    int delta;
    
    if (addr < cluster.start || addr > cluster_end(cluster))
        return NULL;
    
    delta = addr_to_delta(cluster, addr);

    for (int i = 0; i < cluster.size; ++i) {
        if (cluster.deltas[i].delta == delta)
            return &cluster.deltas[i];
    }
    
    return NULL;
}

int AddrSequence::allocate_buf(int count)
{
    buf_type* new_buf_ptr;

    //todo: catch exception for fail case
    new_buf_ptr = bufs.allocate(count);

    bufs_ptr_recorder.push_back(new_buf_ptr);

    return 0;
}

void AddrSequence::free_all_buf()
{
    for(auto& i : bufs_ptr_recorder)
        bufs.deallocate(i, 1);

    bufs_ptr_recorder.clear();
}



//self-testing
#ifdef ADDR_SEQ_SELF_TEST
int main(int argc, char* argv[])
{
    AddrSequence  as;
    int ret_val;
    int i;
    
    as.set_pageshift(12);
    ret_val = as.inc_payload(0x1000, 0);
    ret_val = as.inc_payload(0x2000, 0);
    ret_val = as.inc_payload(0x1000 + 4096 * 255, 1);
    ret_val = as.inc_payload(0x1000 + 4096 * 256, 1);

    as.clear();

    as.rewind();
    
    as.set_pageshift(12);
    for (i = 0; i < 16; ++i)
    {
        ret_val = as.inc_payload(0x1000 + 4096*i, 1);
    }
    
    as.rewind();
    
    for (i = 0; i < 16; ++i)
    {
        ret_val = as.inc_payload(0x1000 + 4096*i, 1);
    }

    as.clear();
    
    return 0;
}
#endif
