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

#include <numa.h>

#include "hmd-common.h"
#include "hmd-config.h"
#include "cmsk.h"

struct hmd_config hmd_config = {
  .pmem_pmu_events = {
    "mem_trans_retired.load_latency_gt_16",
  },
  .dram_pmu_events = {
    "mem_trans_retired.load_latency_gt_16",
  },
  .imc_dram_read = "cas_count_read",
  .imc_dram_write = "cas_count_write",
  .target_pid = -1,
  .sample_period_min = 1000,
  .phy_addr = false,
  .cpumask = NULL,
  .expected_samples = 20000,
  .expected_samples_margin_percent = 20,

  /* default granularity is page size */
  .granularity_order = PAGE_SHIFT,
  .unit_interval_ms = 1000,
  .interval_max = 10,

  .cmsk_cms_width_order = 13,
  .cmsk_achash_size_order = 14,
  .cmsk_achash_threshold = 8,
  .aging_method = CMSK_AGING_CLEAR,
  .hash_mode = false,

  .numa = {
    .numa_dram_list = "0",
    .numa_pmem_list = "1",
    .pmem_dram_map = "1->0",
  },

  .dram_watermark_percent = 10,

  .move_pages_max = 1024,

  .show_only = 0,
  .verbose = 0,
};
