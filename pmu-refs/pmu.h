/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
 */

#ifndef __PMU__HH__
#define  __PMU__HH__

#include <vector>
#include <iterator>
#include <type_traits>
#include <memory>

#include <sys/select.h>
#include <linux/perf_event.h>

#include "perf-iter.h"

#include "hmd-config.h"
#include "Numa.h"

using PmuEventAttrs = std::vector<std::unique_ptr<struct perf_event_attr>>;

struct sample_stats {
  long samples, others, throttled, skipped, lost;
};

class PmuPmemSampleProcessor {
 public:
  virtual bool on_pmu_pmem_sample(u64 value, int pid, int tid, int cpu) = 0;
};

class PmuCpu
{
 public:
  PmuCpu(int cpuid);
  ~PmuCpu();

  int cpuid(void) { return cpuid_; }

  static void setup_pmem_attrs(PmuEventAttrs& attrs, int sample_period);
  void open_pmem_events(const PmuEventAttrs& attrs);
  void close_pmem_events(void);
  void enable_pmem_events(void);
  void disable_pmem_events(void);
  void setup_pmem_sample_period(int sample_period);
  bool read_pmem_samples(fd_set *fd_set, struct sample_stats *st,
                         PmuPmemSampleProcessor *processor);
  void setup_pmem_fd_set(fd_set& fd_set, int& max_fd);

  static void setup_dram_attrs(PmuEventAttrs& attrs);
  void open_dram_events(const PmuEventAttrs& attrs);
  void close_dram_events(void);
  void enable_dram_events(void);
  void disable_dram_events(void);
  void read_dram_count(u64 *local_count, u64 *remote_count);

 private:
  bool read_pmem_samples_fd(struct perf_fd *fd, struct sample_stats *st,
                            PmuPmemSampleProcessor *processor);

 private:
  std::vector<perf_fd> pmem_fds_;
  std::vector<perf_fd> dram_fds_;
  std::vector<u64> dram_counts_;
  int cpuid_;
};

class PmuNode
{
 public:
  PmuNode(int nid) :
	nid_(nid) {}

  int id(void) { return nid_; }
  u64 get_read_KBps(void) { return read_KBps_; }
  u64 get_write_KBps(void) { return write_KBps_; }
  void open_imc_events(int cpu);
  void close_imc_events(void);
  void enable_imc_events(void);
  void read_imc_count(unsigned long ns);

  void reset_dram_count()
  {
    dram_count_ = 0;
    dram_count_avg_ = 0;
  }

  void add_dram_count(u64 val)
  {
    dram_count_ += val;
  }

  void calc_dram_count_avg(NumaNodeCollection *numa_nodes);
  u64 get_dram_count_avg() { return dram_count_avg_; }
  void print_statistics(void);

 private:
  int resolve_imc_event(int imc, const char *base, struct perf_event_attr *attr);

 private:
  int nid_;
  u64 dram_count_;
  u64 dram_count_avg_;
  std::vector<struct perf_fd> imc_read_fds_;
  std::vector<struct perf_fd> imc_write_fds_;
  std::vector<u64> imc_read_prev_;
  std::vector<u64> imc_write_prev_;
  u64 read_KBps_;
  u64 write_KBps_;
};

class PmuState
{
 public:
  void init(NumaNodeCollection *numa_nodes);
  u64 get_pmem_sample_period(void) { return pmem_sample_period_; }

  PmuNode *get_node(int nid)
  {
    return node_map_.at(nid).get();
  }

  PmuNode& operator[](int nid)
  {
    return *get_node(nid);
  }

  void set_pmem_sample_processor(PmuPmemSampleProcessor *processor)
  {
    pmem_sample_processor_ = processor;
  }

  void cpus_init(void);
  void nodes_init(void);
  void open_pmem_events(void);
  void close_pmem_events(void);
  void enable_pmem_events(void);
  void open_dram_events(void);
  void close_dram_events(void);
  void enable_dram_events(void);
  void disable_dram_events(void);
  void open_imc_events(void);
  void close_imc_events(void);
  void begin_interval(void);
  void setup_pmem_sample_period(int sample_period);
  void adjust_sample_period_commit(void);
  bool pmem_samples_enough(void);
  void begin_interval_unit(void);
  bool read_pmem_samples(int interval_ms);
  void read_dram_count(void);
  void read_imc_count();
  bool adjust_sample_period_prepare(void);
  void calc_dram_count_avg(void);
  void print_statistics(void);

  const std::vector<PmuCpu *>& get_cpus() const { return cpus_; }
  const std::vector<PmuNode *>& get_nodes() const { return nodes_; }

 private:
  std::vector<PmuCpu *> cpus_;
  /* manage the life cycle of PmuCpu allocated */
  std::vector<std::unique_ptr<PmuCpu>> cpus_mgr_;
  std::vector<PmuNode *> nodes_;
  /* map from node ID to PmuNode*, and manage their life cycle */
  std::vector<std::unique_ptr<PmuNode>> node_map_;
  NumaNodeCollection *numa_nodes_;
  int pmem_sample_period_;
  int pmem_sample_period_next_;
  int pmem_max_fd_;
  int pmem_expected_samples_margin_;
  long pmem_samples_unit_begin_;
  struct sample_stats stats_;
  unsigned long timestamp_;
  fd_set pmem_open_fds_;
  PmuPmemSampleProcessor *pmem_sample_processor_;
};

#endif /* __PMU__HH__ */
