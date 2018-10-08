#include <stdlib.h>
#include <iostream>

#include "AddrSequence.h"
#include "string.h"

using namespace std;

AddrSequence::AddrSequence()
{
  addr_size = 0;
  nr_walks = 0;
  pageshift = 0;
  pagesize = 0;
  top_bytes = 0;
  young_bytes = 0;

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
  top_bytes = 0;
  young_bytes = 0;
  addr_clusters.clear();
  free_all_buf();

  reset_iterator(find_iter, 0);

#ifdef ADDR_SEQ_SELF_TEST
  test_map.clear();
#endif

}

void AddrSequence::set_pageshift(int shift)
{
  pageshift = shift;
  pagesize = 1 << shift;
}

int AddrSequence::rewind()
{
  reset_iterator(find_iter, 0);

  if (pageshift == 21 && young_bytes)
    printf("nr_walks: %d  young: %'lu  top: %'lu all: %'lu\n",
           nr_walks, young_bytes, top_bytes, addr_size << pageshift);

  ++nr_walks;
  last_cluster_end = 0;
  top_bytes = 0;
  young_bytes = 0;

  return 0;
}

int AddrSequence::inc_payload(unsigned long addr, int n)
{
  int ret_value;

  if (in_append_period())
    ret_value = append_addr(addr, n);
  else
    ret_value = update_addr(addr, n);

  return ret_value;
}

int AddrSequence::update_addr(unsigned long addr, int n)
{
  int rc = 0;
  unsigned long next_addr;
  uint8_t unused_payload;

  for(;;) {
      rc = do_walk(find_iter, next_addr, unused_payload);

      if (!do_walk_continue(rc))
          break;

      if (next_addr == addr) {
          do_walk_update_payload(find_iter, addr, n);
          return 0;
      } else if (next_addr > addr) {
          return ADDR_NOT_FOUND;
      }

      do_walk_move_next(find_iter);
  }

  //in update stage, the addr not exist is a graceful error
  //so we change to return positive ADDR_NOT_FOUND
  if (rc == END_OF_SEQUENCE)
    rc = ADDR_NOT_FOUND;

  return rc;
}

int AddrSequence::smooth_payloads()
{
  for (auto &ac: addr_clusters)
  {
    int runavg;
    //    AddrCluster& ac = kv;
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
  bool is_empty = addr_clusters.empty();

  if (!is_empty)
    reset_iterator(walk_iter, 0);

  return !is_empty;
}

int AddrSequence::get_first(unsigned long& addr, uint8_t& payload)
{
  if (!prepare_get())
    return -1;
  return get_next(addr, payload);
}

int AddrSequence::get_next(unsigned long& addr, uint8_t& payload)
{
  int rc;

  rc = do_walk(walk_iter, addr, payload);
  if (do_walk_continue(rc))
    do_walk_move_next(walk_iter);

  return rc;
}

int AddrSequence::do_walk(walk_iterator& iter,
                          unsigned long& addr, uint8_t& payload)
{
  if (iter.cluster_iter == iter.cluster_iter_end)
    return END_OF_SEQUENCE;

  unsigned long delta_sum;

  delta_sum = iter.delta_sum + iter.cur_delta_ptr[iter.delta_index].delta;
  addr = iter.cur_cluster_ptr->start + (delta_sum << pageshift);
  payload = iter.cur_delta_ptr[iter.delta_index].payload;

  return 0;
}

void AddrSequence::do_walk_move_next(walk_iterator& iter)
{
  iter.delta_sum += iter.cur_delta_ptr[iter.delta_index].delta;

  ++iter.delta_index;
  if (iter.delta_index >= iter.cur_cluster_ptr->size) {
    iter.delta_index = 0;
    iter.delta_sum = 0;
    ++iter.cluster_iter;

    if(iter.cluster_iter < iter.cluster_iter_end) {
        do_walk_update_current_ptr(iter);
    }
  }
}

void AddrSequence::do_walk_update_payload(walk_iterator& iter,
                                          unsigned addr, uint8_t payload)
{
  // payload == 0: ignore
  if (!payload)
    return;

  // payload == 1: increase
  ++iter.cur_delta_ptr[iter.delta_index].payload;
  if (iter.cur_delta_ptr[iter.delta_index].payload >= nr_walks)
    top_bytes += pagesize;
  young_bytes += pagesize;
}

int AddrSequence::append_addr(unsigned long addr, int n)
{
  if (addr_clusters.empty())
    return create_cluster(addr, n);

  // the addr in append stage should grow from low to high
  // and never duplicated or rollback, so here we
  // return directly
  if (addr <= last_cluster_end)
    return IGNORE_DUPLICATED_ADDR;

  AddrCluster& cluster = addr_clusters.back();
  if (can_merge_into_cluster(cluster, addr))
    return save_into_cluster(cluster, addr, n);
  else
    return create_cluster(addr, n);
}


int AddrSequence::create_cluster(unsigned long addr, int n)
{
  void* new_buf_ptr;
  int rc;

  rc = get_free_buffer(&new_buf_ptr);
  if (rc < 0)
    return rc;

  try {
    addr_clusters.push_back(new_cluster(addr, new_buf_ptr));
  } catch(std::bad_alloc& e) {
    return -ENOMEM;
  }
  //a new cluster created, so update this
  //new cluster's end = strat of cause
  last_cluster_end = addr;

  //set the find iterator because we added new cluster
  reset_iterator(find_iter, addr_clusters.size() - 1);

  return save_into_cluster(addr_clusters.back(), addr, n);
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
  int rc = 0;

  if (is_buffer_full())
    rc = allocate_buf();

  if (rc >= 0) {
    DeltaPayload* ptr;
    ptr = buf_pool.back();
    *free_ptr = &ptr[buf_used_count];
  }

  return rc;
}


int AddrSequence::save_into_cluster(AddrCluster& cluster,
                                    unsigned long addr, int n)
{
  unsigned long delta = (addr - last_cluster_end) >> pageshift;
  int index = cluster.size;

  cluster.deltas[index].delta = (uint8_t)delta;
  cluster.deltas[index].payload = n;

  if (n) {
      top_bytes += pagesize;
    young_bytes += pagesize;
  }

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

  if (delta_distance > MAX_DELTA_DIST
      || is_buffer_full()
      || is_not_align)
    return false;

  return true;
}

int AddrSequence::allocate_buf()
{
  DeltaPayload* new_buf_ptr = NULL;

  try {
    new_buf_ptr = buf_allocator.allocate(MAX_ITEM_COUNT);

    //new free buffer saved to back of pool
    //users will always use back() to get it later
    buf_pool.push_back(new_buf_ptr);

    //we already have a new buf in pool,
    //so let's reset the buf_used_count here
    buf_used_count = 0;

  } catch(std::bad_alloc& e) {
    if (new_buf_ptr)
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

int AddrSequence::do_self_test_compare(unsigned long pagesize, bool is_perf)
{
  unsigned long addr;
  uint8_t payload = 0;

  cout << "addr_clusters.size = " << addr_clusters.size() << endl;
  cout << "buf_pool.size = " << buf_pool.size() << endl;

  if (!prepare_get()) {
    cout << "empty AddrSequence" << endl;
    return -1;
  }

  if (is_perf) {
    while (!get_next(addr, payload))
        ;
    return 0;
  }

  for (auto& kv: test_map)
  {
    int err = get_next(addr, payload);
    if (err < 0)
      return err;

    if (addr != kv.first) {
      fprintf(stderr, "addr mismatch: %lx != %lx\n page increment=%lx",
              addr, kv.first, pagesize);
      return -1;
    }
    if (payload != kv.second) {
      fprintf(stderr, "payload mismatch: Addr = %lx, %d != %d page increment=%lx\n",
              kv.first, (int)payload, (int)kv.second,
              pagesize);
      return -2;
    }
  }

  return 0;
}

int AddrSequence::do_self_test_walk(unsigned long pagesize, bool is_perf)
{
  unsigned long addr = 0x100000;
  unsigned long delta;
  bool is_first_walk = test_map.empty();

  rewind();
  for (int i = 0; i < 1<<20; ++i)
  {
    delta = is_perf ? 1 : rand() & 0xff;
    addr += delta * pagesize;
    int val = is_perf ? 1 : (rand() & 1);

    int err = inc_payload(addr, val);
    if (err < 0) {
      fprintf(stderr, "inc_payload error %d\n pagesize increment=%lx", err, pagesize);
      fprintf(stderr, "nr_walks=%d i=%d addr=%lx val=%d\n", nr_walks, i, addr, val);
      return err;
    }

    if (is_perf)
        continue;

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
    }
  }

  return 0;
}

int AddrSequence::self_test()
{
  int ret;

  clear();
  set_pageshift(12);
  ret = do_self_test(4096, 30, true);
  if (ret)
    return ret;

  clear();
  set_pageshift(21);
  ret = do_self_test(1<<21, 30, true);
  if (ret)
    return ret;

  return ret;
}

int AddrSequence::do_self_test(unsigned long pagesize,
                               int max_loop,
                               bool is_perf)
{
  std::map<unsigned long, uint8_t> am;
  int err;

  //max_walks = 30; //rand() & 0xff;
  for (int i = 0; i < max_loop; ++i)
  {
    err = do_self_test_walk(pagesize, is_perf);
    if (err < 0)
      return err;
  }
  err = do_self_test_compare(pagesize, is_perf);
  if (err < 0)
    return err;
  err = do_self_test_compare(pagesize, is_perf);
  return err;
}


int test_static()
{
  AddrSequence  as;
  int rc;
  unsigned long addr;
  uint8_t  payload;

  as.set_pageshift(12);
  rc = as.inc_payload(0x1000, 0);
  rc = as.inc_payload(0x3000, 0);
  rc = as.inc_payload(0x5000, 0);
  rc = as.inc_payload(0x8000, 0);
  rc = as.inc_payload(0x1000 + 4096 * 255, 1);
  rc = as.inc_payload(0x1000 + 4096 * 256, 1);

  as.clear();

  as.rewind();

  as.set_pageshift(12);
  rc = as.inc_payload(0x11000, 0);
  rc = as.inc_payload(0x13000, 0);
  rc = as.inc_payload(0x15000, 0);
  rc = as.inc_payload(0x18000, 0);
  rc = as.inc_payload(0x21000, 0);
  rc = as.inc_payload(0x23000, 0);
  rc = as.inc_payload(0x25000, 0);
  rc = as.inc_payload(0x28000, 0);
  rc = as.inc_payload(0x30000, 0);
  rc = as.inc_payload(0x32000, 0);

  as.rewind();
  rc = as.inc_payload(0x11000, 1);
  rc = as.inc_payload(0x13000, 0);
  rc = as.inc_payload(0x15000, 1);
  rc = as.inc_payload(0x18000, 0);
  rc = as.inc_payload(0x21000, 1);
  rc = as.inc_payload(0x23000, 0);
  rc = as.inc_payload(0x25000, 1);
  rc = as.inc_payload(0x28000, 1);
  rc = as.inc_payload(0x30000, 0);
  rc = as.inc_payload(0x32000, 1);
  rc = as.inc_payload(0x40000, 1); //should not update
  rc = as.inc_payload(0x40000, 1); //should not update

  rc = as.get_first(addr, payload);
  while(!rc) {
    printf("addr = %lx, payload = %u\n", addr, payload);
    rc = as.get_next(addr, payload);
  }

  as.clear();

  as.clear();
  as.set_pageshift(12);
  rc = as.do_self_test(1, rand() & 127, false);
  if (rc)
    goto out;
  
  as.clear();
  as.set_pageshift(12);
  rc = as.do_self_test(4096, rand() & 127, false);
  if (rc)
    goto out;

  as.clear();
  as.set_pageshift(21);
  rc = as.do_self_test(1<<21, 30, false);

out:
  return rc;
}

int main(int argc, char* argv[])
{
  if (argc >= 2) {
    if (!strcmp(argv[1], "check")) {
      return test_static();
    }
  }

  AddrSequence as;
  return as.self_test();
}

#endif
