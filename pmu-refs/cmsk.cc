/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <climits>

#include <algorithm>

#include "cmsk.h"

static const unsigned long hasha[] = {
  0x303d3429, 0x39b02a15, 0x22984495, 0x330d690f,
  0x37f09895, 0x2c4967f1, 0x3eacfedd, 0x24753fa3,
  0x2684579f, 0x361962b5, 0x2e714de9, 0x29ae22e9,
  0x117433d5, 0x143e3891, 0x21deb47f, 0x28df71e1,
  0x2f633edf, 0x12addc0d, 0x1b9d2fc1, 0x12fded03,
  0x2d9758a7, 0x2a34b455, 0x3172715b, 0x148b815b,
  0x28c9b9fb, 0x17a31d75, 0x15fe7b69, 0x26987d49,
  0x25e2440f, 0x1b4ab775, 0x2992a0d7, 0x2903b75f,
};

static const unsigned long hashb[] = {
  0x382a9bb5, 0x14ea0b57, 0x3d09e275, 0x2fe39ce7,
  0x3d359391, 0x17638f3f, 0x3725344f, 0x3baee8dd,
  0x3e4c6b57, 0x39f57e4b, 0x119725c1, 0x154d3bf9,
  0x3cf63121, 0x221fe771, 0x34255333, 0x3a9dff77,
  0x2e607ae3, 0x359b44f5, 0x379f3a65, 0x366f4147,
  0x2d10d9e9, 0x37ff0489, 0x3a4d42dd, 0x313f83a1,
  0x26642525, 0x244ef881, 0x1d663f5d, 0x1ada7899,
  0x1f896d3d, 0x23d43f21, 0x3e128f1f, 0x3d329827,
};

static const unsigned long hashp[] = {
  0x329449f1, 0x342b5385, 0x13e7b215, 0x1094d12d,
  0x1a8fad3b, 0x2b66f2f5, 0x2b24b01d, 0x29fde021,
  0x28a9b9fd, 0x21e3bb4d, 0x2ed4524f, 0x215b1017,
  0x33ae4a5d, 0x17c569c9, 0x2e32fa01, 0x1e913343,
  0x1c41f0dd, 0x2271c37f, 0x2e534adb, 0x22eaf111,
  0x1b3b265d, 0x18c8c5a3, 0x21880fd5, 0x14d3b879,
  0x39e4f58f, 0x1b2e22c5, 0x31bb8bdd, 0x32e9cacb,
  0x25d22a8b, 0x11eff19b, 0x32f5b809, 0x10144961,
};

static int cms_init(struct cms *cms)
{
  unsigned int i, width;

  width = 1 << cms->width_order;
  cms->width_mask = width - 1;
  cms->total = 0;
  cms->matrix.resize(cms->depth);
  for (i = 0; i < cms->depth; i++)
    cms->matrix[i].resize(width);

  return 0;
}

static void cms_fini(struct cms *cms)
{
}

static void cms_clear(struct cms *cms)
{
  unsigned int i;

  for (i = 0; i < cms->depth; i++)
    std::fill(cms->matrix[i].begin(), cms->matrix[i].end(), 0);
  cms->total = 0;
}

static void cms_age(struct cms *cms)
{
  unsigned int i, j, width;

  width = 1 << cms->width_order;
  for (i = 0; i < cms->depth; i++)
    for (j = 0; j < width; j++)
      cms->matrix[i][j] >>= 1;
  cms->total >>= 1;
}

static inline unsigned int hash(unsigned long long item1,
				unsigned long long item2,
				int i)
{
  return (item1 * hasha[i] + item2 * hashb[i]) / hashp[i];
}

static unsigned int cms_error(struct cms *cms)
{
  /* P(error > 2N/w) < (1/2)**d */
  return cms->total >> (cms->width_order - 1);
}

static unsigned int cms_update(struct cms *cms, unsigned long item1,
			       unsigned long item2, unsigned int *ph)
{
  unsigned int i;
  unsigned int h = 0;
  cms_count_t val, cmin = CMS_COUNT_MAX;

  for (i = 0; i < cms->depth; i++) {
    h = hash(item1, item2, i) & cms->width_mask;
    val = ++cms->matrix[i][h];
    cmin = val < cmin ? val : cmin;
  }
  *ph = h;
  cms->total++;

  return cmin;
}

static int achash_init(struct achash *achash)
{
  if (achash->size_order > ACHASH_SIZE_ORDER_MAX)
    return -1;
  achash->size = 1 << achash->size_order;
  achash->mask = achash->size - 1;
  achash->len = 0;
  achash->samples = 0;
  achash->items.resize(achash->size);
  achash->buckets.resize(achash->size);
  achash->hist.resize(achash->hist_max);

  return 0;
}

static void achash_fini(struct achash *achash)
{
}

static void achash_clear(struct achash *achash)
{
  achash->len = 0;
  achash->samples = 0;
  std::fill(achash->buckets.begin(), achash->buckets.end(), 0);
  std::fill(achash->hist.begin(), achash->hist.end(), 0);
}

static inline int hist_index(struct achash *achash, unsigned int count)
{
  int index = count;

  if (index >= achash->hist_max)
    index = achash->hist_max - 1;

  return index;
}

static void hist_inc(struct achash *achash, unsigned int count)
{
  int index = hist_index(achash, count);

  achash->hist[index]++;
}

static void hist_dec(struct achash *achash, unsigned int count)
{
  int index = hist_index(achash, count);

  achash->hist[index]--;
}

static unsigned int achash_new_item(struct achash *achash,
				    unsigned long addr,
				    unsigned int pid)
{
  struct achash_item *item;

  item = &achash->items[achash->len];
  item->addr = addr;
  item->pid = pid;
  item->next = 0;
  if (achash->hash_mode) {
    item->count = 1;
    achash->samples++;
    hist_inc(achash, 1);
  } else {
    item->count = achash->threshold;
    achash->samples += achash->threshold;
  }

  return ++achash->len;
}

static inline struct achash_item *achash_item(struct achash *achash,
					      unsigned int num)
{
  return &achash->items[num - 1];
}

static bool achash_update(struct achash *achash, unsigned long addr,
			  unsigned int pid, unsigned int hash)
{
  unsigned int num;
  struct achash_item *item;

  hash &= achash->mask;
  num = achash->buckets[hash];
  if (!num) {
    achash->buckets[hash] = achash_new_item(achash, addr, pid);
    return achash->len >= achash->size;
  }
  while (num) {
    item = achash_item(achash, num);
    if (item->addr == addr && item->pid == pid) {
      item->count++;
      achash->samples++;
      hist_inc(achash, item->count);
      hist_dec(achash, item->count - 1);
      return false;
    }
    num = item->next;
  }
  item->next = achash_new_item(achash, addr, pid);
  return achash->len >= achash->size;
}

static void achash_age(struct achash *achash)
{
  achash_clear(achash);
}

static bool achash_cmp(const struct achash_item& itema,
                       const struct achash_item& itemb)
{
  return itema.count > itemb.count;
}

static void achash_sort(struct achash *achash)
{
  std::sort(achash->items.begin(), achash->items.end(), achash_cmp);
}

static unsigned int achash_bsearch(struct achash *achash, unsigned int end,
				   cms_count_t count)
{
  unsigned int start = 0, mid;
  cms_count_t mcnt;

  for (;;) {
    if (start == end)
      return start;
    mid = (start + end) / 2;
    mcnt = achash->items[mid].count;
    if (mcnt <= count) {
      end = mid;
    } else if (mcnt > count) {
      start = mid + 1;
    }
  }
}

void hist_print(struct achash *achash)
{
  unsigned int hist_total = 0;

  for (int i = achash->hist_max - 1; i >= 0; i--) {
    if (!achash->hist[i])
      continue;

    hist_total += achash->hist[i] * i;
    printf("hist-%d: %u\n", i, achash->hist[i]);
  }

  printf("hist total: %u\n", hist_total);
}

static void achash_print(struct achash *achash, unsigned int total)
{
  unsigned int i, j, n, samples, size, nsize;

  samples = achash->samples;
  size = achash->len;

  /* To avoid to be divided by 0 */
  total = total ? : 1;

  printf("hot size: %u\n", size);
  printf("hot weight: %.2f%%\n", samples * 100.0 / total);

  n = 1;
  for (i = 0; i < sizeof(cms_count_t) * 8; i++, n <<= 1) {
    nsize = achash_bsearch(achash, size, n);
    if (nsize == size)
      continue;
    for (j = nsize; j < size; j++)
      samples -= achash->items[j].count;
    printf("hot size > %d: %u\n", n, nsize);
    printf("hot weight > %d: %.2f%%\n", n, samples * 100.0 / total);
    size = nsize;
  }

  printf("hot pages:\n");
  for (i = 0; i < std::min(8U, achash->len); i++) {
    auto item = &achash->items[i];
    printf("  %lx %lu: %lu, %.2f%%\n", item->addr, item->pid, item->count,
           double(item->count) * 100 / total);
  }
}

int cmsk_init(struct cmsk *cmsk)
{
  int ret;

  memset(&cmsk->stats, 0, sizeof(struct cmsk_stats));
  cmsk->no = cmsk->interval;
  ret = cms_init(&cmsk->cms);
  if (ret)
    return ret;
  ret = achash_init(&cmsk->achash);
  if (ret)
    return ret;

  return 0;
}

void cmsk_fini(struct cmsk *cmsk)
{
  cms_fini(&cmsk->cms);
  achash_fini(&cmsk->achash);
}

void cmsk_clear(struct cmsk *cmsk)
{
  if (!cmsk->achash.hash_mode)
    cms_clear(&cmsk->cms);
  else
    cmsk->cms.total = 0;
  achash_clear(&cmsk->achash);
}

bool cmsk_update(struct cmsk *cmsk, unsigned long item1, unsigned long item2)
{
  cms_count_t count;
  unsigned int h;
  struct cms *cms = &cmsk->cms;
  struct achash *achash = &cmsk->achash;

  if (achash->hash_mode) {
    h = hash(item1, item2, cms->depth - 1);
    cms->total++;
    return achash_update(achash, item1, item2, h);
  }

  count = cms_update(cms, item1, item2, &h);
  if (count >= achash->threshold + cms_error(cms))
    return achash_update(achash, item1, item2, h);
  return false;
}

void cmsk_age(struct cmsk *cmsk)
{
  if (cmsk->aging_method == CMSK_AGING_HALF) {
    cms_age(&cmsk->cms);
    achash_age(&cmsk->achash);
  } else
    cmsk_clear(cmsk);
}

void cmsk_sort(struct cmsk *cmsk)
{
  struct achash *achash = &cmsk->achash;
  struct cms *cms = &cmsk->cms;
  unsigned int threshold, len = achash->len;

  if (!len) {
    cmsk->stats.nr_hot_page = 0;
    return;
  }

  achash_sort(&cmsk->achash);

  threshold = achash->threshold;
  if (!achash->hash_mode)
    threshold += cms_error(cms);
  if (achash->items[len - 1].count < threshold) {
    unsigned int i, olen = achash->len;
    achash->len = achash_bsearch(achash, len - 1, threshold - 1);
    for (i = achash->len; i < olen; i++)
      achash->samples -= achash->items[i].count;
  }

  cmsk->stats.nr_hot_page = achash->len;
}

static int achash_cmp_pid(const void *a, const void *b)
{
  const struct achash_item *itema = static_cast<const struct achash_item *>(a),
                           *itemb = static_cast<const struct achash_item *>(b);

  return itemb->pid - itema->pid;
}

void cmsk_sort_by_pid(struct achash_item *items, unsigned int n)
{
  qsort(items, n, sizeof(items[0]), achash_cmp_pid);
}

struct achash_item *cmsk_hot_pages(struct cmsk *cmsk, unsigned int *n)
{
  struct achash *achash = &cmsk->achash;

  if (*n > achash->len)
    *n = achash->len;
  if (!*n)
    return NULL;

  return achash->items.data();
}

void cmsk_print(struct cmsk *cmsk)
{
  unsigned int total;

  total = cmsk->cms.total;
  printf("total: %u\n", total);
  if (!total)
    return;
  printf("error_threshold: %u\n", cms_error(&cmsk->cms));

  achash_print(&cmsk->achash, total);
}
