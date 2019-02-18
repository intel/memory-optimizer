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

#ifndef __ADDRESS_RANGE_FILTER__HH__
#define __ADDRESS_RANGE_FILTER__HH__

#include <iterator>
#include <type_traits>
#include <map>
#include <string>

class AddressRangeFilter
{
  struct Range {
    unsigned long size;
  };

  class Key
  {
    public:
      unsigned long pid;
      unsigned long start;

    public:
      Key(unsigned long p, unsigned long s) :
        pid(p), start(s) {}

      Key(void) :
        pid(0), start(0) {}

      bool operator<(const Key& src) const
      {
        if (pid == src.pid)
          return start < src.start;
        else
          return pid < src.pid;
      }
  };

  typedef std::map<Key, Range>::iterator Iterator;

  public:
    bool search_address(int pid, unsigned long addr);
    void insert_range(int pid, unsigned long start, unsigned long size);
    void clear(void);
    void show(void);

  private:
    Iterator search_range(int pid, unsigned long addr,
        Iterator *lower_p, Iterator *upper_p);
    void insert_new_range(int pid, unsigned long start, unsigned long size);
    void remove_ranges(Iterator rm_start, Iterator rm_end,
        int pid, unsigned long new_start, unsigned long new_end);

  private:
    std::map<Key, Range> rmap;
};

#endif /* __ADDRESS_RANGE_FILTER__HH__ */
