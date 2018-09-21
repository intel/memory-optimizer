#include <stdlib.h>
#include <iostream>

#include "AddrSequence.h"

using namespace std;

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
  delta_update_index = 0;
  delta_update_sum = 0;
}

void AddrSequence::set_pageshift(int shift)
{
  pageshift = shift;
  pagesize = 1 << shift;
}

int AddrSequence::rewind()
{
  ++nr_walks;

  iter_update = addr_clusters.begin();
  delta_update_sum = 0;
  delta_update_index = 0;
  current_cluster_end = 0;
  
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
    int ret_val = 1;
    int is_done = 0;
    unsigned long each_addr;
    
    while(iter_update != addr_clusters.end()) {
        AddrCluster& cluster = iter_update->second;

        is_done = 0;
        while(delta_update_index < cluster.size) {
            delta_update_sum += cluster.deltas[delta_update_index].delta;
            each_addr = cluster.start + delta_update_sum * pagesize;

            if (each_addr == addr) {

                //got it
                if (n)
                    ++cluster.deltas[delta_update_index].payload;
                else
                    cluster.deltas[delta_update_index].payload = 0;

                //do NOT move!
                delta_update_sum -= cluster.deltas[delta_update_index].delta;
                ret_val = 0;
                is_done = 1;
                break;

            } else if (each_addr > addr) {
                //do NOT move!
                delta_update_sum -= cluster.deltas[delta_update_index].delta;

                //in update period, can NOT find addr is graceful fail, so just return
                //postive value
                ret_val = 1;
                is_done = 1;
                break;
            }

            ++delta_update_index;
        }

        if (is_done)
            break;

        ++iter_update;
        cluster_change_for_update();
    }

    return ret_val;
}

bool AddrSequence::prepare_get()
{
    iter_cluster = addr_clusters.begin();
    iter_delta_index = 0;
    iter_delta_val = 0;
    return iter_cluster != addr_clusters.end();
}

int AddrSequence::get_first(unsigned long& addr, uint8_t& payload)
{
  if (!prepare_get())
    return -1;
  return get_next(addr, payload);
}

int AddrSequence::get_next(unsigned long& addr, uint8_t& payload)
{
  if (iter_cluster == addr_clusters.end())
    return -1;

  AddrCluster &cluster = iter_cluster->second;
  DeltaPayload *delta_ptr = cluster.deltas;

  iter_delta_val += delta_ptr[iter_delta_index].delta;
  addr = cluster.start + (iter_delta_val << pageshift);
  payload = delta_ptr[iter_delta_index].payload;

  if (++iter_delta_index >= cluster.size) {
    ++iter_cluster;
    iter_delta_index = 0;
    iter_delta_val = 0;
  }

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
        if (addr > cluster_end(cluster)) {
            if (can_merge_into_cluster(cluster, addr)) 
                ret_val = save_into_cluster(cluster, addr, n);
            else
                ret_val = create_cluster(addr, n);
        } else {
            // the addr in nr_walks = 1 stage should grow from low to high,
            // so all address <= last inserted address will ignore here
            // with return 2
            ret_val = 2;
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

    new_buf_ptr = get_free_buffer();
    if (!new_buf_ptr)
      return -1;

    std::pair<unsigned long, AddrCluster>
      new_item(addr, new_cluster(addr, new_buf_ptr));

    auto insert_ret = addr_clusters.insert(new_item);
    if (!insert_ret.second)
      return -1;

    //a new cluster created, so update this
    //new cluster's end = strat of cause
    current_cluster_end = addr;

    //set the find iterator because we added new cluster
    cluster_added_for_update(insert_ret.first);

    return save_into_cluster(insert_ret.first->second, addr, n);
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
    unsigned long delta = addr_to_delta(cluster, addr);
    int index = cluster.size;
    
    if (delta > 255) {
        printf("overflow error!\n");
        return -1;
    }

    cluster.deltas[index].delta = (uint8_t)delta;
    cluster.deltas[index].payload = n;

    ++cluster.size;
    ++buf_item_used;
    ++addr_size;

    //becuase cluster grow at end always
    current_cluster_end = addr;
    
    return 0;
}

int AddrSequence::can_merge_into_cluster(AddrCluster& cluster, unsigned long addr)
{
    unsigned long addr_delta = addr - cluster_end(cluster);
    unsigned long pagecount = addr_delta >> pageshift;
    int is_not_align = addr_delta & (pagesize - 1);

    if (pagecount > 255
        || is_buffer_full()
        || is_not_align)
        return false;

    return true;
}



DeltaPayload* AddrSequence::addr_to_delta_ptr(AddrCluster& cluster,
                                              unsigned long addr)
{
    unsigned long delta_addr;
    unsigned long end_addr = cluster_end(cluster);
    
    if (addr < cluster.start || addr > end_addr)
        return NULL;

    delta_addr = cluster.start;
    for (int i = 0; i < cluster.size; ++i) {
        delta_addr += cluster.deltas[i].delta * pagesize;

        if (delta_addr == addr)
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

int AddrSequence::self_test_compare()
{
  unsigned long addr;
  uint8_t payload;

  cout << "self_test_compare" << endl;

  if (!prepare_get()) {
    cout << "empty AddrSequence" << endl;
    return -1;
  }

  for (auto& kv: test_map)
  {
    int err = get_next(addr, payload);
    if (err < 0)
      return err;

    if (addr != kv.first) {
      fprintf(stderr, "addr mismatch: %lx != %lx\n", addr, kv.first);
      return -1;
    }
    if (payload != kv.second) {
      fprintf(stderr, "payload mismatch: Addr = %lx, %d != %d\n",
              kv.first, (int)payload, (int)kv.second);
      return -2;
    }
  }

  return 0;
}

int AddrSequence::self_test_walk()
{
  unsigned long addr = 0x100000;
  unsigned long delta;
  bool is_first_walk = test_map.empty();

  rewind();
  for (int i = 0; i < 1<<20; ++i)
  {
    delta = rand() & 0xff;
    addr += delta;
    int val = rand() & 1;

    int err = inc_payload(addr, val);
    if (err < 0) {
      fprintf(stderr, "inc_payload error %d\n", err);
      fprintf(stderr, "nr_walks=%d i=%d addr=%lx val=%d\n", nr_walks, i, addr, val);
      return err;
    }

    /*
      the addr may duplicated beacsue rand() may return 0
      because we ignore this, so we only put non-duplicated addr
      into test_map 
    */
    if (is_first_walk) {
      if (!err)
        test_map[addr] = val;
    } else if(test_map.find(addr) != test_map.end()) {
        /*
          for the nr_walk >=2 case, update stage, we do
          change payload(base on val) if addr already exists,
          so change here to same as inc_payload() behavior.
         */        
        if (val)
          ++test_map[addr];
        else
          test_map[addr] = val;
    }
  }
  
  return 0;
}

int AddrSequence::self_test()
{
  std::map<unsigned long, uint8_t> am;
  int max_walks;
  int err;

  clear();
  set_pageshift(12);

  max_walks = rand() & 0xff;
  for (int i = 0; i < max_walks; ++i)
  {
    err = self_test_walk();
    if (err < 0)
      return err;
  }
  err = self_test_compare();
  return err;
}

void test_static()
{
  AddrSequence  as;
  int ret_val;
  unsigned long addr;
  uint8_t  payload;

  as.set_pageshift(12);
  ret_val = as.inc_payload(0x1000, 0);
  ret_val = as.inc_payload(0x3000, 0);
  ret_val = as.inc_payload(0x5000, 0);
  ret_val = as.inc_payload(0x8000, 0);
  ret_val = as.inc_payload(0x1000 + 4096 * 255, 1);
  ret_val = as.inc_payload(0x1000 + 4096 * 256, 1);

  as.clear();

  as.rewind();

  as.set_pageshift(12);
  ret_val = as.inc_payload(0x11000, 0);
  ret_val = as.inc_payload(0x13000, 0);
  ret_val = as.inc_payload(0x15000, 0);
  ret_val = as.inc_payload(0x18000, 0);
  ret_val = as.inc_payload(0x21000, 0);
  ret_val = as.inc_payload(0x23000, 0);
  ret_val = as.inc_payload(0x25000, 0);
  ret_val = as.inc_payload(0x28000, 0);
  ret_val = as.inc_payload(0x30000, 0);
  ret_val = as.inc_payload(0x32000, 0);

  as.rewind();
  ret_val = as.inc_payload(0x11000, 1);
  ret_val = as.inc_payload(0x13000, 0);
  ret_val = as.inc_payload(0x15000, 1);
  ret_val = as.inc_payload(0x18000, 0);
  ret_val = as.inc_payload(0x21000, 1);
  ret_val = as.inc_payload(0x23000, 0);
  ret_val = as.inc_payload(0x25000, 1);
  ret_val = as.inc_payload(0x28000, 1);
  ret_val = as.inc_payload(0x30000, 0);
  ret_val = as.inc_payload(0x32000, 1);
  ret_val = as.inc_payload(0x40000, 1); //should not update
  ret_val = as.inc_payload(0x40000, 1); //should not update

  ret_val = as.get_first(addr, payload);
  while(!ret_val) {
    printf("addr = %lx, payload = %u\n", addr, payload);
    ret_val = as.get_next(addr, payload);
  }

  as.clear();
}

int main(int argc, char* argv[])
{
#if 1
  AddrSequence as;
  return as.self_test();
#else
  test_static();
  return 0;
#endif
}

#endif
