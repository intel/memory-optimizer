/*
 * Count-Min Sketch
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

#ifndef __CMSK__H__
#define __CMSK__H__

#include <climits>
#include <vector>

typedef unsigned short cms_count_t;
#define CMS_COUNT_MAX	USHRT_MAX

enum {
  CMSK_AGING_CLEAR,
  CMSK_AGING_HALF,
};

struct cms
{
  unsigned char width_order;
  unsigned int width_mask;
  unsigned int depth;
  unsigned int total;
  std::vector<std::vector<cms_count_t>> matrix;
};

#define ACHASH_SIZE_ORDER_MAX	23

struct achash_item
{
  unsigned long addr;
  unsigned long pid:24;
  unsigned long next:24;
  unsigned long count:16;
};

/* Address/count Hash */
struct achash
{
  unsigned int threshold;
  unsigned int size_order;
  unsigned int size;
  unsigned int mask;
  unsigned int len;
  unsigned int samples;
  std::vector<unsigned> buckets;
  std::vector<struct achash_item> items;
};

/* Top-K frequent items with Cout-Min-Sketch */
struct cmsk
{
  bool hash_mode;
  int aging_method;
  unsigned int interval;
  unsigned int no;
  struct cms cms;
  struct achash achash;
};

int cmsk_init(struct cmsk *cmsk);
void cmsk_fini(struct cmsk *cmsk);
void cmsk_clear(struct cmsk *cmsk);
bool cmsk_update(struct cmsk *cmsk, unsigned long item1, unsigned long item2);
void cmsk_age(struct cmsk *cmsk);
void cmsk_sort(struct cmsk *cmsk);
void cmsk_print(struct cmsk *cmsk);
struct achash_item *cmsk_hot_pages(struct cmsk *cmsk, unsigned int *n);
void cmsk_sort_by_pid(struct achash_item *items, unsigned int n);

#endif /* __CMSK__H__ */
