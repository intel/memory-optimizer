/*
 * HMEM Migration Daemon filter test
 *
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <cassert>
#include <unistd.h>
#include "../AddressRangeFilter.h"

int main(int argc, char *argv[])
{
  AddressRangeFilter filter;

  filter.clear();

  filter.insert_range(1, 1000, 1000);
  filter.insert_range(1, 2000, 1000);
  filter.insert_range(1, 0000, 1000);
  filter.insert_range(2, 1000, 1000);
  filter.insert_range(2, 3000, 1000);
  filter.insert_range(2, 1000, 2000);
  filter.insert_range(3, 4000, 1000);
  filter.insert_range(3, 3000, 1000);
  filter.insert_range(1, 2000, 1999);

  filter.show();

  if (!filter.search_address(1, 3999))
    printf("PASSED: Can't find pid 1/address 3999\n");
  else
    printf("FAILED: Find pid 1/address 3999\n");

  if (filter.search_address(1, 3998))
    printf("PASSED: Find pid 1/address 3998\n");
  else
    printf("FAILED: Can't find pid 1/address 3998\n");

  if (filter.search_address(3, 3091))
    printf("PASSED: Find pid 3/address 3091\n");
  else
    printf("FAILED: Can't find pid 3/address 3091\n");

  return 0;
}
