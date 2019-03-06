/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
 */

#ifndef __HMD_CONFIG__HH__
#define __HMD_CONFIG__HH__

#include "Option.h"

enum pmu_event_type {
  PET_LOCAL,
  PET_REMOTE,
  PET_NUMBER
};

struct bitmask;

struct addr_range {
  int pid;
  unsigned long start;
  unsigned long size;
};

struct hmd_config {
  const char *pmem_pmu_events[PET_NUMBER];
  const char *dram_pmu_events[PET_NUMBER];
  const char *imc_dram_read;
  const char *imc_dram_write;
  int target_pid;
  int sample_period_min;
  bool phy_addr;
  struct bitmask *cpumask;
  int expected_samples;
  int expected_samples_margin_percent;

  int granularity_order;
  int unit_interval_ms;
  int interval_max;

  int cmsk_cms_width_order;
  int cmsk_achash_size_order;
  int cmsk_achash_threshold;
  int aging_method;
  bool hash_mode;
  bool imc_counting;

  NumaHWConfig numa;

  int dram_watermark_percent;

  int move_pages_max;

  int show_only;
  int verbose;

  int arfilter_reset_intervals;

  /*
   * Should be defined at the end of struct, otherwise g++ reports
   * "non-trivial designated initializers not supported"
   */
  std::vector<addr_range> arfilter;
};

extern struct hmd_config hmd_config;

#endif /* __HMD_CONFIG__HH__ */
