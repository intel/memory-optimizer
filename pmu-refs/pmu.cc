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
#include <cstdio>

#include <sys/ioctl.h>
#include <numa.h>

#include "jevents.h"

#include "hmd-common.h"
#include "hmd-config.h"
#include "pmu.h"
#include "migration.h"

/* 2^n size of event ring buffer (in pages) */
#define BUF_SIZE_SHIFT	8

static void pmu_open_events(const PmuEventAttrs &attrs,
                            std::vector<perf_fd>& fds,
                            int cpuid)
{
  for (unsigned i = 0; i < attrs.size(); i++) {
    if (attrs[i] &&
	perf_fd_open_other(&fds[i], attrs[i].get(),
                           BUF_SIZE_SHIFT,
                           hmd_config.target_pid,
                           cpuid) < 0) {
      sys_err("open perf event");
    }
  }
}

static void pmu_close_events(std::vector<perf_fd>& fds)
{
  for (auto& fd: fds) {
    if (fd.pfd > -1) {
      if (perf_disable(&fd) < 0)
        sys_err("PERF_EVENT_IOC_DISABLE");
      perf_fd_close(&fd);
      fd.pfd = -1;
    }
  }
}

static void pmu_enable_events(std::vector<perf_fd>& fds)
{
  for (auto& fd: fds) {
    if (fd.pfd > -1) {
      if (perf_enable(&fd) < 0)
        sys_err("PERF_EVENT_IOC_ENABLE");
    }
  }
}

static void pmu_disable_events(std::vector<perf_fd>& fds)
{
  for (auto& fd: fds) {
    if (fd.pfd > -1) {
      if (perf_disable(&fd) < 0)
        sys_err("PERF_EVENT_IOC_DISABLE");
    }
  }
}

PmuCpu::PmuCpu(int cpuid) :
	pmem_fds_(PET_NUMBER), dram_fds_(PET_NUMBER),
	dram_counts_(PET_NUMBER), cpuid_(cpuid)
{
  for (auto& fd: pmem_fds_)
    fd.pfd = -1;

  for (auto& fd: dram_fds_)
    fd.pfd = -1;
}

PmuCpu::~PmuCpu()
{
  pmu_close_events(pmem_fds_);
  pmu_close_events(dram_fds_);
}

void PmuCpu::setup_pmem_attrs(PmuEventAttrs& attrs, int sample_period)
{
  attrs.resize(PET_NUMBER);
  for (int i = 0; i < PET_NUMBER; i++) {
    const char *event = hmd_config.pmem_pmu_events[i];
    if (!event)
      continue;

    auto attr = new perf_event_attr;
    attrs[i].reset(attr);
    if (resolve_event(event, attr) < 0) {
      fprintf(stderr, "cannot resolve %s\n", event);
      exit(1);
    }
    attr->sample_period = sample_period;
    attr->exclude_kernel = 1;
    attr->disabled = 1;
    attr->precise_ip = 1;
    attr->sample_type = hmd_config.phy_addr ? PERF_SAMPLE_PHYS_ADDR :
                        PERF_SAMPLE_ADDR | PERF_SAMPLE_TID;
  }
}


void PmuCpu::open_pmem_events(const PmuEventAttrs& attrs)
{
  pmu_open_events(attrs, pmem_fds_, cpuid_);
}

void PmuCpu::close_pmem_events(void)
{
  pmu_close_events(pmem_fds_);
}

void PmuCpu::enable_pmem_events(void)
{
  pmu_enable_events(pmem_fds_);
}

void PmuCpu::disable_pmem_events(void)
{
  pmu_disable_events(pmem_fds_);
}

void PmuCpu::setup_pmem_sample_period(int sample_period)
{
  u64 period = sample_period;

  for (auto& fd: pmem_fds_) {
    if (fd.pfd > -1) {
      if (ioctl(fd.pfd, PERF_EVENT_IOC_PERIOD, &period) < 0)
        sys_err("PERF_EVENT_IOC_PERIOD");
    }
  }
}

bool PmuCpu::read_pmem_samples_fd(struct perf_fd *fd, struct sample_stats *st,
                                  PmuPmemSampleProcessor *processor)
{
  struct perf_iter iter;
  unsigned int sample_size = hmd_config.phy_addr ? 16 : 24;
  bool full = false;
  u64 val;
  u64 pid = 1;
  int tid = 1;
  char buffer[64];
  struct perf_event_header *hdr;

  perf_iter_init(&iter, fd);
  while (!perf_iter_finished(&iter)) {
    hdr = perf_buffer_read(&iter, buffer, sizeof(buffer));
    if (!hdr) {
      st->skipped++;
      continue;
    }

    if (hdr->type != PERF_RECORD_SAMPLE) {
      if (hdr->type == PERF_RECORD_THROTTLE)
        st->throttled++;
      else if (hdr->type == PERF_RECORD_LOST)
        st->lost += perf_hdr_payload(hdr)[1];
      else
        st->others++;
      continue;
    }
    st->samples++;
    if (hdr->size != sample_size) {
      printf("unexpected sample size %d\n", hdr->size);
      continue;
    }

    if (hmd_config.phy_addr) {
      val = perf_hdr_payload(hdr)[0];
    } else {
      pid = perf_hdr_payload(hdr)[0];
      val = perf_hdr_payload(hdr)[1];

      /* Filter out kernel samples, which can happen due to OOO skid */
      if ((s64)val <= 0)
        continue;

      /*
       * struct perf_sample_data {
       *	...
       *	struct {
       *		u32	pid;
       *		u32	tid;
       *	} tid_entry;
       *	...
       * }
       */
      tid = pid >> 32;
      pid &= 0xffffffff;
      if (!pid)
        continue;
    }
    full = processor->on_pmu_pmem_sample(val, pid, tid, cpuid_);
    if (full)
      break;
  }
  perf_iter_continue(&iter);

  return full;
}

bool PmuCpu::read_pmem_samples(fd_set *fd_set, struct sample_stats *st,
                               PmuPmemSampleProcessor *processor)
{
  for (auto& fd: pmem_fds_) {
    int pfd = fd.pfd;
    if (pfd > -1) {
      if (!fd_set || FD_ISSET(pfd, fd_set)) {
        if (read_pmem_samples_fd(&fd, st, processor))
          return true;
        if (fd_set)
          perf_enable(&fd);
      }
    }
  }

  return false;
}

void PmuCpu::setup_pmem_fd_set(fd_set& fd_set, int& max_fd)
{
  for (auto& fd: pmem_fds_) {
    if (fd.pfd > -1) {
      FD_SET(fd.pfd, &fd_set);
      if (fd.pfd > max_fd)
        max_fd = fd.pfd;
    }
  }
}

void PmuCpu::setup_dram_attrs(PmuEventAttrs& attrs)
{
  attrs.resize(PET_NUMBER);
  for (int i = 0; i < PET_NUMBER; i++) {
    const char *event = hmd_config.dram_pmu_events[i];
    if (!event)
      continue;

    auto attr = new perf_event_attr;
    attrs[i].reset(attr);
    if (resolve_event(event, attr) < 0) {
      fprintf(stderr, "cannot resolve %s\n", event);
      exit(1);
    }
    attr->exclude_kernel = 1;
    attr->disabled = 1;
    attr->sample_type = PERF_SAMPLE_READ;
  }
}

void PmuCpu::open_dram_events(const PmuEventAttrs& attrs)
{
  pmu_open_events(attrs, dram_fds_, cpuid_);
}

void PmuCpu::close_dram_events(void)
{
  pmu_close_events(dram_fds_);
}

void PmuCpu::enable_dram_events(void)
{
  pmu_enable_events(dram_fds_);
}

void PmuCpu::disable_dram_events(void)
{
  pmu_disable_events(dram_fds_);
}

void PmuCpu::read_dram_count(u64 *local_count, u64 *remote_count)
{
  u64 val, diff;
  struct perf_fd *fd;

  for (unsigned int i = 0; i < dram_fds_.size(); i++) {
    fd = &dram_fds_[i];
    if (fd->pfd > -1) {
      if (read_all(fd->pfd, &val, sizeof(val)) < 0)
        sys_err("read dram count");

      diff = val - dram_counts_[i];
      if (i == PET_LOCAL)
        *local_count = diff;
      else
        *remote_count = diff;
      dram_counts_[i] = val;
    }
  }
}

int PmuNode::resolve_imc_event(int imc, const char *base,
                               struct perf_event_attr *attr)
{
  char event[128];

  snprintf(event, sizeof(event), "uncore_imc_%d/%s/", imc, base);

  if (resolve_event(event, attr) < 0) {
    if (imc == 0) {
      snprintf(event, sizeof(event), "uncore_imc/%s/", base);
      if (resolve_event(event, attr) < 0)
        return -1;
    } else
      return -1;
  }

  return 0;
}

void PmuNode::open_imc_events(int cpu)
{
  int i;
  struct perf_fd fd;
  struct perf_event_attr imc_read_attr, imc_write_attr;

  for (i = 0; /* rely on break */ ; i++) {
    if ((resolve_imc_event(i, hmd_config.imc_dram_read, &imc_read_attr) < 0) ||
        (resolve_imc_event(i, hmd_config.imc_dram_write, &imc_write_attr) < 0))
      break;

    imc_read_attr.disabled = 1;
    imc_write_attr.disabled = 1;

    if (perf_fd_open_other(&fd, &imc_read_attr, BUF_SIZE_SHIFT, -1, cpu) < 0) {
      sys_err("open perf event for imc read bandwidth");
    }
    imc_read_fds_.push_back(fd);

    if (perf_fd_open_other(&fd, &imc_write_attr, BUF_SIZE_SHIFT, -1, cpu) < 0) {
      sys_err("open perf event for imc write bandwidth");
    }
    imc_write_fds_.push_back(fd);
  }

  imc_read_prev_.resize(imc_read_fds_.size());
  imc_write_prev_.resize(imc_write_fds_.size());

  enable_imc_events();
}

void PmuNode::close_imc_events(void)
{
  pmu_close_events(imc_read_fds_);
  pmu_close_events(imc_write_fds_);
}

void PmuNode::read_imc_count(unsigned long ns)
{
  u64 val, diff;

  read_KBps_ = 0;
  write_KBps_ = 0;

  for (unsigned i = 0; i < imc_read_fds_.size(); i++) {
    /*
     * struct read_format {
     *      { u64   value; }
     * };
     */
    if (read_all(imc_read_fds_[i].pfd, &val, sizeof(val)) < 0)
      sys_err("read imc_read count");

    diff = val - imc_read_prev_[i];
    imc_read_prev_[i] = val;
    read_KBps_ += diff;

    if (read_all(imc_write_fds_[i].pfd, &val, sizeof(val)) < 0)
      sys_err("read imc_write count");

    diff = val - imc_write_prev_[i];
    imc_write_prev_[i] = val;
    write_KBps_ += diff;
  }

  /* read_KBps * 64 * NS_PER_SEC / ts_diff / 1024 */
  read_KBps_ = read_KBps_ * (NS_PER_SEC / 16) / ns;
  write_KBps_ = write_KBps_ * (NS_PER_SEC / 16) / ns;
}

void PmuNode::enable_imc_events(void)
{
  pmu_enable_events(imc_read_fds_);
  pmu_enable_events(imc_write_fds_);
}

void PmuNode::calc_dram_count_avg(NumaNodeCollection *numa_nodes)
{
  NumaNode *node = numa_nodes->get_node(nid_);
  unsigned long used = node->mem_used() >> hmd_config.granularity_order;

  if (used)
    dram_count_avg_ = dram_count_ / used;
}

void PmuNode::print_statistics(void)
{
  printf("node %d imc read %lld KB/s, write %lld KB/s\n",
         nid_, read_KBps_, write_KBps_);
  printf("node %d memory accesses: %llu, avg: %llu\n",
         nid_, dram_count_, dram_count_avg_);
}

void PmuState::open_pmem_events(void)
{
  PmuEventAttrs attrs;

  pmem_sample_period_ = hmd_config.sample_period_min;
  FD_ZERO(&pmem_open_fds_);

  PmuCpu::setup_pmem_attrs(attrs, pmem_sample_period_);

  for (auto& cpu: cpus_) {
    cpu->open_pmem_events(attrs);
    cpu->setup_pmem_fd_set(pmem_open_fds_, pmem_max_fd_);
  }
}

void PmuState::close_pmem_events(void)
{
  for (auto& cpu: cpus_) {
    cpu->close_pmem_events();
  }
}

void PmuState::enable_pmem_events(void)
{
  for (auto& cpu: cpus_) {
    cpu->enable_pmem_events();
  }
}

void PmuState::open_dram_events(void)
{
  PmuEventAttrs attrs;

  PmuCpu::setup_dram_attrs(attrs);

  for (auto& cpu: cpus_) {
    cpu->open_dram_events(attrs);
  }
}

void PmuState::close_dram_events(void)
{
  for (auto& cpu: cpus_) {
    cpu->close_dram_events();
  }
}

void PmuState::enable_dram_events(void)
{
  for (auto& cpu: cpus_) {
    cpu->enable_dram_events();
  }
}

void PmuState::disable_dram_events(void)
{
  for (auto& cpu: cpus_) {
    cpu->disable_dram_events();
  }
}

void PmuState::open_imc_events(void)
{
  int cpu;

  timestamp_ = rdclock();

  for (auto& node: nodes_) {
    cpu = numa_nodes_->get_node_lowest_cpu(node->id());
    if (cpu > -1) {
      node->open_imc_events(cpu);
      node->enable_imc_events();
    }
  }
}

void PmuState::close_imc_events(void)
{
  for (auto& node: nodes_) {
    node->close_imc_events();
  }
}

void PmuState::begin_interval(void)
{
  memset(&stats_, 0, sizeof(stats_));
  for (auto& node: nodes_) {
    node->reset_dram_count();
  }
}

void PmuState::setup_pmem_sample_period(int sample_period)
{
  int prev_period = pmem_sample_period_;

  for (auto& cpu: cpus_) {
    cpu->setup_pmem_sample_period(sample_period);
  }

  pmem_sample_period_ = sample_period;

  if (hmd_config.verbose)
    printf("Adjust sample period: %d -> %d\n",
           prev_period, sample_period);
}

void PmuState::adjust_sample_period_commit(void)
{
  if (!pmem_sample_period_next_)
    return;

  setup_pmem_sample_period(pmem_sample_period_next_);
  pmem_sample_period_next_ = 0;
}

bool PmuState::pmem_samples_enough(void)
{
  return stats_.samples >=
      hmd_config.expected_samples - pmem_expected_samples_margin_;
}

void PmuState::begin_interval_unit(void)
{
  pmem_samples_unit_begin_ = stats_.samples;
}

bool PmuState::read_pmem_samples(int interval_ms)
{
  bool full = false;
  int n;
  unsigned long ts;
  unsigned long end;
  fd_set read_fds;

  end = rdclock() + interval_ms * NS_PER_MSEC;
  while ((ts = rdclock()) < end) {
    struct timeval tv;
    tv.tv_sec = 0;
    if (end > ts)
      tv.tv_usec = (end - ts) / 1000;
    else
      tv.tv_usec = 0;

    read_fds = pmem_open_fds_;
    n = select(pmem_max_fd_, &read_fds, NULL, NULL, &tv);
    if (n < 0) {
      perror("select");
      break;
    }

    for (auto& cpu: cpus_) {
      full = cpu->read_pmem_samples(&read_fds, &stats_, pmem_sample_processor_);
      if (full)
        goto end_measure;
    }
  }

end_measure:
  for (auto& cpu: cpus_) {
    cpu->disable_pmem_events();
    if (!full)
      full = cpu->read_pmem_samples(nullptr, &stats_, pmem_sample_processor_);
  }

  return full;
}

void PmuState::read_dram_count(void)
{
  int dram_node_count = numa_nodes_->get_dram_nodes().size();

  for (auto& cpu: cpus_) {
    u64 local_count = 0, remote_count = 0;
    cpu->read_dram_count(&local_count, &remote_count);
    NumaNode *node = numa_nodes_->node_of_cpu(cpu->cpuid());
    int nid = node->id();
    PmuNode *pnode = get_node(nid);
    pnode->add_dram_count(local_count);
    for (auto &onode: numa_nodes_->get_dram_nodes()) {
      int onid = onode->id();
      if (onid == nid)
        continue;
      PmuNode *opnode = get_node(onid);
      /*
       * Distribute remote access count evenly, that is not perfect
       * for machines with more than 2 sockets
       */
      if (dram_node_count > 1)
        opnode->add_dram_count(remote_count / (dram_node_count - 1));
    }
  }
}

void PmuState::read_imc_count()
{
  unsigned long curr_ts, ts_diff;

  curr_ts = rdclock();
  ts_diff = curr_ts - timestamp_;
  timestamp_ = curr_ts;

  for (auto& node: nodes_) {
    node->read_imc_count(ts_diff);
  }
}

void PmuState::cpus_init(void)
{
  PmuCpu *pcpu;
  int nr_possible_cpu;
  struct bitmask *cpumask;

  nr_possible_cpu = numa_num_possible_cpus();
  cpumask = hmd_config.cpumask ? : numa_all_cpus_ptr;

  for (int i = 0; i < nr_possible_cpu; i++) {
    if (numa_bitmask_isbitset(cpumask, i)) {
      pcpu = new PmuCpu(i);
      cpus_mgr_.push_back(std::unique_ptr<PmuCpu>(pcpu));
      cpus_.push_back(pcpu);
    }
  }
}

void PmuState::nodes_init(void)
{
  PmuNode *pnode;

  node_map_.resize(numa_nodes_->nr_possible_node());
  for (auto& node: *numa_nodes_) {
    int nid = node->id();
    pnode = new PmuNode(nid);
    node_map_[nid].reset(pnode);
    nodes_.push_back(pnode);
  }
}

bool PmuState::adjust_sample_period_prepare(void)
{
  int new_period, old_period = pmem_sample_period_;
  int samples_margin = pmem_expected_samples_margin_;
  long samples;

  samples = stats_.samples - pmem_samples_unit_begin_;
  if (samples > hmd_config.expected_samples + samples_margin ||
      (samples < hmd_config.expected_samples - samples_margin &&
       old_period > hmd_config.sample_period_min)) {
    new_period = samples * old_period / hmd_config.expected_samples;
    if (new_period < hmd_config.sample_period_min)
      new_period = hmd_config.sample_period_min;
    pmem_sample_period_next_ = new_period;
    return true;
  }

  return false;
}

void PmuState::calc_dram_count_avg(void)
{
  for (auto& node: nodes_)
    node->calc_dram_count_avg(numa_nodes_);
}

void PmuState::print_statistics(void)
{
  for (auto& node: nodes_) {
    node->print_statistics();
  }

  printf("%s: %ld samples, %ld others, %lu lost, %ld throttled, %ld skipped\n",
         hmd_config.pmem_pmu_events[PET_LOCAL],
         stats_.samples,
         stats_.others,
         stats_.lost,
         stats_.throttled,
         stats_.skipped);
}

void PmuState::init(NumaNodeCollection *numa_nodes)
{
  numa_nodes_ = numa_nodes;
  memset(&stats_, 0, sizeof(stats_));
  cpus_init();
  nodes_init();
}
