/*
 * Copyright (c) 2018 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "AddressRangeFilter.h"

AddressRangeFilter::Iterator AddressRangeFilter::search_range(
    int pid, unsigned long addr,
    AddressRangeFilter::Iterator *lower_p,
    AddressRangeFilter::Iterator *upper_p)
{
  Key k(pid, addr);
  Range r;
  auto lower = rmap.end(), upper = rmap.end(), it = rmap.end();

  if (rmap.empty())
    goto exit;

  upper = rmap.upper_bound(k);
  it = upper;
  it--;
  if (it == upper) {
    it = rmap.end();
    goto exit;
  }

  k = it->first;
  r = it->second;

  if (k.pid != (unsigned long)pid) {
    it = rmap.end();
    goto exit;
  }

  if (addr < k.start + r.size) {
    /* Range is found, try to get the lower range */
    lower = it;
    lower--;
    if (lower == it) {
      /* It's possible when it and lower are all rmap.begin() */
      lower = rmap.end();
    }
    goto exit;
  }

  lower = it;
  it = rmap.end();

exit:
  if (lower_p)
    *lower_p = lower;

  if (upper_p)
    *upper_p = upper;

  return it;
}

bool AddressRangeFilter::search_address(int pid, unsigned long addr)
{
  Iterator it;

  it = search_range(pid, addr, NULL, NULL);
  return it != rmap.end();
}

void AddressRangeFilter::insert_new_range(int pid, unsigned long start,
    unsigned long size)
{
  Key k(pid, start);
  Range r;

  r.size = size;
  rmap.insert(std::pair<Key, Range>(k, r));
}

void AddressRangeFilter::remove_ranges(
    AddressRangeFilter::Iterator rm_start, AddressRangeFilter::Iterator rm_end,
    int pid, unsigned long new_start, unsigned long new_end)
{
  Iterator begin = rm_start;
  Iterator it;

  if (begin == rmap.end())
    begin = rmap.begin();

  it = begin;
  while (it != rmap.end() && it != rm_end) {
    Key k;
    Range r;

    k = it->first;
    r = it->second;

    if ((unsigned long)pid != k.pid) {
      it++;
      continue;
    }

    if (new_start <= k.start && new_end >= k.start + r.size) {
      it = rmap.erase(it);
      continue;
    }

    it++;
  }

  if (it != rmap.end())
    rmap.erase(it);
}

void AddressRangeFilter::insert_range(int pid, unsigned long start,
    unsigned long size)
{
  Iterator rm_start, rm_end, lower, upper;
  unsigned long new_start = start, new_end = start + size;
  Key k;
  Range r;

  /*
   *   $lower$    |new|
   * $---------$ |-----|
   *   $lower$    |new|
   * $---------|-$-----|
   */
  rm_start = search_range(pid, start, &lower, NULL);

  if (rm_start != rmap.end()) {
    k = rm_start->first;
    new_start = k.start;
  } else if (lower != rmap.end()) {
    rm_start = lower;
    k = rm_start->first;
    r = rm_start->second;

    if ((unsigned long)pid == k.pid && k.start + r.size == start)
      new_start = k.start;
    else
      rm_start++;
  }

  /*
   *   |new|      $upper$
   * |---------| $-------$
   *   |new|      $upper$
   * |---------$-|-------$
   */
  rm_end = search_range(pid, start + size - 1, NULL, &upper);

  if (rm_end != rmap.end()) {
    k = rm_end->first;
    r = rm_end->second;
    new_end = k.start + r.size;
  } else if (upper != rmap.end()) {
    k = upper->first;
    r = upper->second;

    if ((unsigned long)pid == k.pid && new_end == k.start) {
      rm_end = upper;
      new_end = k.start + r.size;
    }
  }

  remove_ranges(rm_start, rm_end, pid, new_start, new_end);
  insert_new_range(pid, new_start, new_end - new_start);
}

void AddressRangeFilter::clear(void)
{
  rmap.clear();
}

void AddressRangeFilter::show(void)
{
  auto it = rmap.begin();

  while (it != rmap.end()) {
    Key k = it->first;
    Range *r = &it->second;

    printf("pid: %d, start: %ld, size: %ld\n", (int)k.pid, k.start, r->size);
    it++;
  }
}
