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
  buf_used_count = MAX_ITEM_COUNT;
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

  auto new_find_iter = addr_clusters.begin();
  reset_find_iterator(new_find_iter);

}

void AddrSequence::set_pageshift(int shift)
{
  pageshift = shift;
  pagesize = 1 << shift;
}

int AddrSequence::rewind()
{
  auto new_find_iter = addr_clusters.begin();
  reset_find_iterator(new_find_iter);

  ++nr_walks;
  last_cluster_end = 0;
  
  return 0;
}

int AddrSequence::inc_payload(unsigned long addr, int n)
{
    int ret_value;

    if (in_append_period()) {
        ret_value = append_addr(addr, n);
    } else {
        ret_value = update_addr(addr, n);
    }

    return ret_value;
}


int AddrSequence::update_addr(unsigned long addr, int n)
{
    //find if the addr already in the cluster, return if not
    unsigned long next_addr;
    unsigned long delta_sum;
    int ret_val = ADDR_NOT_FOUND;
    int is_done = 0;

    while(find_iter != addr_clusters.end()) {
        AddrCluster& cluster = find_iter->second;

        is_done = 0;
        while(find_delta_index < cluster.size) {

            delta_sum = find_delta_sum
                        + cluster.deltas[find_delta_index].delta;
            next_addr = cluster.start + delta_sum * pagesize;

            if (next_addr == addr) {

                if (n)
                    ++cluster.deltas[find_delta_index].payload;
                else
                    cluster.deltas[find_delta_index].payload = 0;

                ret_val = 0;
                is_done = 1;
                break;
            } else if (next_addr > addr) {

                //in update period, can NOT find addr is graceful fail, so just return
                //postive value
                ret_val = ADDR_NOT_FOUND;
                is_done = 1;
                break;
            }

            find_delta_sum = delta_sum;
            ++find_delta_index;
        }

        if (is_done)
            break;

        reset_find_delta_iterator();
        ++find_iter;
    }

    return ret_val;
}

int AddrSequence::smooth_payloads()
{
  for (auto &kv: addr_clusters)
  {
    int runavg;
    AddrCluster& ac = kv.second;
    for (int i = 0; i < ac.size; ++i)
    {
      if (!i || ac.deltas[i].delta > 3)
        runavg = ac.deltas[i].payload;
      else {
        runavg = (7 + runavg * 7 + ac.deltas[i].payload) / 8;
        ac.deltas[i].payload = runavg;
      }
    }
  }
  return 0;
}

bool AddrSequence::prepare_get()
{
    walk_iter = addr_clusters.begin();
    walk_delta_index = 0;
    walk_delta_sum = 0;

    return true;
}

int AddrSequence::get_first(unsigned long& addr, uint8_t& payload)
{
  if (!prepare_get())
    return -1;
  return get_next(addr, payload);
}

int AddrSequence::get_next(unsigned long& addr, uint8_t& payload)
{
    if (walk_iter != addr_clusters.end()) {
        int ret_val = 0;
        AddrCluster &cluster = walk_iter->second;
        DeltaPayload *delta_ptr = cluster.deltas;

        if (walk_delta_index >= cluster.size) {
            walk_delta_index = 0;
            walk_delta_sum = 0;
            ++walk_iter;

            // check again because we moved get_cluster
            if (walk_iter == addr_clusters.end())
                return -1;

            cluster = walk_iter->second;
            delta_ptr = cluster.deltas;
        }

        walk_delta_sum += delta_ptr[walk_delta_index].delta;
        addr = cluster.start + walk_delta_sum * pagesize;
        payload = delta_ptr[walk_delta_index].payload;

        ++walk_delta_index;

        ret_val = 0;
    }

  return 0;
}

int AddrSequence::append_addr(unsigned long addr, int n)
{
    int ret_val;
    auto last = addr_clusters.rbegin();

    if (last == addr_clusters.rend()) {
      return create_cluster(addr, n);
    }

    if (addr > last_cluster_end) {
        AddrCluster& cluster = last->second;

        ret_val = 0;
        if (can_merge_into_cluster(cluster, addr))
          save_into_cluster(cluster, addr, n);
        else
          ret_val = create_cluster(addr, n);

    } else {
      // the addr in append stage should grow from low to high
      // and never duplicated or rollback, so here we
      // return directly
      ret_val = IGNORE_DUPLICATED_ADDR;
    }

    return ret_val;
}


int AddrSequence::create_cluster(unsigned long addr, int n)
{
    void* new_buf_ptr;
    int ret_val;
    
    ret_val = get_free_buffer(&new_buf_ptr);
    if (ret_val < 0)
        return ret_val;

    std::pair<unsigned long, AddrCluster>
      new_item(addr, new_cluster(addr, new_buf_ptr));

    auto insert_ret = addr_clusters.insert(new_item);
    if (!insert_ret.second)
        return CREATE_CLUSTER_FAILED;

    //a new cluster created, so update this
    //new cluster's end = strat of cause
    last_cluster_end = addr;

    //set the find iterator because we added new cluster
    reset_find_iterator(insert_ret.first);

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


int AddrSequence::get_free_buffer(void** free_ptr)
{
  int ret_val = 0;

  if (is_buffer_full())
    ret_val = allocate_buf();

  if (ret_val >= 0) {
    DeltaPayload* ptr;
    ptr = buf_pool.back();
    *free_ptr = &ptr[buf_used_count];
  }

  return ret_val;
}


int AddrSequence::save_into_cluster(AddrCluster& cluster,
                                    unsigned long addr, int n)
{
    unsigned long delta = (addr - last_cluster_end) >> pageshift;
    int index = cluster.size;

    cluster.deltas[index].delta = (uint8_t)delta;
    cluster.deltas[index].payload = n;

    ++cluster.size;
    ++buf_used_count;
    ++addr_size;

    //becuase cluster grow at end always
    last_cluster_end = addr;

    return 0;
}

bool AddrSequence::can_merge_into_cluster(AddrCluster& cluster, unsigned long addr)
{
    unsigned long addr_delta = addr - last_cluster_end;
    unsigned long delta_distance = addr_delta >> pageshift;
    int is_not_align = addr_delta & (pagesize - 1);

    if (delta_distance > 255
        || is_buffer_full()
        || is_not_align)
        return false;

    return true;
}

int AddrSequence::allocate_buf()
{
    DeltaPayload* new_buf_ptr;

    try {
      new_buf_ptr = buf_allocator.allocate(MAX_ITEM_COUNT);
    } catch(std::bad_alloc& e) {
      return -ENOMEM;
    }

    try {
      //new free buffer saved to back of pool
      //users will always use back() to get it later
      buf_pool.push_back(new_buf_ptr);

      //we already have a new buf in pool,
      //so let's reset the buf_used_count here
      buf_used_count = 0;
    } catch (std::bad_alloc& e) {
      buf_allocator.deallocate(new_buf_ptr, MAX_ITEM_COUNT);
      return -ENOMEM;
    }

    return 0;
}

void AddrSequence::free_all_buf()
{
    for(auto& i : buf_pool)
        buf_allocator.deallocate(i, MAX_ITEM_COUNT);

    buf_pool.clear();
    buf_used_count = MAX_ITEM_COUNT;
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
