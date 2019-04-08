/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
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
  bool hash_mode;
  unsigned int threshold;
  unsigned int size_order;
  unsigned int size;
  unsigned int mask;
  unsigned int len;
  unsigned int samples;
  std::vector<unsigned> buckets;
  std::vector<struct achash_item> items;
};

struct cmsk_stats
{
  unsigned int nr_hot_page;
};

/* Top-K frequent items with Cout-Min-Sketch */
struct cmsk
{
  int aging_method;
  unsigned int interval;
  unsigned int no;
  struct cms cms;
  struct achash achash;
  struct cmsk_stats stats;
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
