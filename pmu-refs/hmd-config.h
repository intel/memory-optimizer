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
