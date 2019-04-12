/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2019 Intel Corporation
 *
 * Authors: Huang Ying <ying.huang@intel.com>
 *          Jin Yao <yao.jin@intel.com>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <cassert>

#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <getopt.h>
#include <sched.h>

#include <numa.h>
#include <numaif.h>

#include "hmd-common.h"
#include "hmd-config.h"
#include "cmsk.h"
#include "Numa.h"
#include "pmu.h"
#include "migration.h"
#include "AddressRangeFilter.h"

static const char *aging_method_names[] = {
  "clear",
  "half",
  NULL,
};

class PMUMemoryOptimizer : public PmuPmemSampleProcessor
{
  NumaNodeCollection numa_nodes_;
  PmuState pmu_state_;
  MigrationState migration_state_;
  struct cmsk cmsk_;
  AddressRangeFilter arfilter_;

  std::vector<unsigned long> hot_pages_;
  std::vector<int> sample_counts_;

  void setup_signal();

  void usage(const char *program);
  int parse_name_option(const char *option, const char *name,
                        const char *all_names[]);
  long parse_long_option(char *str, char **av);
  void parse_options(int ac, char *av[]);
  int parse_filter_addr_range_option(char *str, char *av);
  void reset_arfilter(void);

  void print_statistics();

  bool on_pmu_pmem_sample(u64 value, int pid, int tid, int cpu);

  void migrate_pages();

 public:
  PMUMemoryOptimizer()
  {
    pmu_state_.set_pmem_sample_processor(this);
  }
  int main(int argc, char *argv[]);
};

int interrupted;

static void on_interrupt(int signum)
{
  interrupted = 1;
}

void PMUMemoryOptimizer::setup_signal()
{
  struct sigaction sa_int;

  memset(&sa_int, 0, sizeof(sa_int));
  sa_int.sa_handler = on_interrupt;
  sa_int.sa_flags = SA_RESTART;

  sigaction(SIGINT, &sa_int, NULL);
  sigaction(SIGTERM, &sa_int, NULL);
}

bool PMUMemoryOptimizer::on_pmu_pmem_sample(
    u64 val, int pid, int tid, int cpu)
{
  val >>= hmd_config.granularity_order;
  return cmsk_update(&cmsk_, val, pid);
}

void PMUMemoryOptimizer::print_statistics()
{
  struct timespec ts;

  clock_gettime(CLOCK_REALTIME, &ts);
  printf("time: %lu.%09lu\n", ts.tv_sec, ts.tv_nsec);

  pmu_state_.print_statistics();

  cmsk_print(&cmsk_);
}

void PMUMemoryOptimizer::migrate_pages()
{
  struct achash_item *items;
  unsigned int npg_shift = hmd_config.granularity_order > PAGE_SHIFT ?
                           hmd_config.granularity_order - PAGE_SHIFT : 0;
  unsigned int n = hmd_config.move_pages_max >> npg_shift;
  unsigned int i, j, moved = 0, pid;
  unsigned long addr;

  pid = 0;
  items = cmsk_hot_pages(&cmsk_, &n);
  if (!n)
    return;

  hot_pages_.clear();
  sample_counts_.clear();

  for (i = 0; i < n; i++) {
    if (items[i].pid != pid) {
      if (pid) {
        moved += migration_state_.move_pages(pid, hot_pages_, sample_counts_);
        hot_pages_.clear();
        sample_counts_.clear();
      }
      pid = items[i].pid;
    }
    addr = (items[i].addr << hmd_config.granularity_order) & PAGE_MASK;
    for (j = 0; j < (1U << npg_shift); j++) {
      if (arfilter_.search_address(pid, addr + j * PAGE_SIZE)) {
        if (hmd_config.verbose)
          printf("Address is filtered (pid: %d, addr: 0x%lx)\n",
		 pid, addr + j * PAGE_SIZE);
        continue;
      }

      sample_counts_.push_back(items[i].count);
      hot_pages_.push_back(addr + j * PAGE_SIZE);
    }
  }
  moved += migration_state_.move_pages(pid, hot_pages_, sample_counts_);
  hot_pages_.clear();
  sample_counts_.clear();

  if (hmd_config.verbose)
    printf("%u pages moved\n", moved);
}

void PMUMemoryOptimizer::usage(const char *program)
{
  fprintf(stderr,
          "Usage: %s options\n"
          "	-e|--pmem-local-event EVENT	PMEM local PMU event to sample\n"
          "	-E|--pmem-remote-event EVENT	PMEM remote PMU event to sample\n"
          "	--dram-local-event EVENT	DRAM local PMU event to count\n"
          "	--dram-remote-event EVENT	DRAM remote PMU event to count\n"
          "	--imc-dram-read-event EVENT	Event for IMC read bandwidth\n"
          "	--imc-dram-write-event EVENT	Event for IMC write bandwidth\n"
          "	--imc-counting			enable imc counting (i.e. memory bandwidth)\n"
          "	--sample-period-min COUNT	minimum perf sample period\n"
          "	-u|--cpu-list CPU_LIST		target CPU list to monitor\n"
          "	-p|--pid PID			target process ID to monitor\n"
          "	-g|--granularity-order ORDER	address granularity in order (page, huge page, etc.)\n"
          "	--unit-interval MS		unit of measurement interval in ms\n"
          "	--interval-max COUNT		maximum measurement interval in unit count\n"
          "	--expected-samples SAMPLES	expected sample count for each interval\n"
          "	--expected-samples-margin-percent PERCENT\n"
          "					percent of expected sample count that real sample\n"
          "					count could be different for each interval\n"
          "	-o|--cms-width-order ORDER	width of CMS matrix in order\n"
          "	--hash-size-order ORDER	size of hash in order\n"
          "	-t|--hash-threshold THRESHOLD	threshold of hash\n"
          "	-a|--aging-method METHOD	the aging method to use: clear or half\n"
          "	--hash-mode			do statistics with hash only, without cms\n"
          "	-m|--move-pages-max COUNT	maximum page count to move for each interval\n"
          "	--dram-count-multiple MULTIPLE  times to enlarge dram count before comparison\n"
          "	--dram-list LIST		DRAM node list\n"
          "	--pmem-list LIST		PMEM node list\n"
          "	--pmem-dram-map MAP		map from PMEM node to nearest DRAM node\n"
          "	--filter-addr-range PID/START:SIZE	add address range to filter\n"
          "	--filter-reset-intervals COUNT	maximum filter reset interval in unit count\n"
          "	--show-only			show statistics only, don't move pages\n"
          "	-v|--verbose			verbose mode\n"
          "	--runtime RUNTIME               maximum run time in seconds\n"
          "	--hist-max NUM                  maximum histogram number\n"
          "	-h|--help			show this message\n",
          program);
  exit(1);
}

enum {
  OPT_START = 256,
  OPT_DRAM_LOCAL_EVENT,
  OPT_DRAM_REMOTE_EVENT,
  OPT_SAMPLE_PERIOD_MIN,
  OPT_UNIT_INTERVAL,
  OPT_INTERVAL_MAX,
  OPT_EXPECTED_SAMPLES,
  OPT_EXPECTED_SAMPLES_MARGIN_PERCENT,
  OPT_HASH_SIZE_ORDER,
  OPT_HASH_MODE,
  OPT_DRAM_COUNT_MULTIPLE,
  OPT_DRAM_LIST,
  OPT_PMEM_LIST,
  OPT_PMEM_DRAM_MAP,
  OPT_SHOW_ONLY,
  OPT_IMC_DRAM_READ_EVENT,
  OPT_IMC_DRAM_WRITE_EVENT,
  OPT_FILTER_ADDR_RANGE,
  OPT_FILTER_RESET_INTERVALS,
  OPT_IMC_COUNTING,
  OPT_RUNTIME,
  OPT_HIST_MAX,
};

static const struct option options[] = {
  { "pmem-local-event", 1, NULL, 'e' },
  { "pmem-remote-event", 1, NULL, 'E' },
  { "dram-local-event", 1, NULL, OPT_DRAM_LOCAL_EVENT },
  { "dram-remote-event", 1, NULL, OPT_DRAM_REMOTE_EVENT },
  { "imc-dram-read-event", 1, NULL, OPT_IMC_DRAM_READ_EVENT },
  { "imc-dram-write-event", 1, NULL, OPT_IMC_DRAM_WRITE_EVENT },
  { "sample-period-min", 1, NULL, OPT_SAMPLE_PERIOD_MIN },
  { "cpu-list", 1, NULL, 'u' },
  { "pid", 1, NULL, 'p' },
  { "granularity-order", 1, NULL, 'g' },
  { "unit-interval", 1, NULL, OPT_UNIT_INTERVAL},
  { "interval-max", 1, NULL, OPT_INTERVAL_MAX },
  { "expected-samples", 1, NULL, OPT_EXPECTED_SAMPLES },
  { "expected-samples-margin-percent", 1, NULL,
    OPT_EXPECTED_SAMPLES_MARGIN_PERCENT },
  { "cms-width-order", 1, NULL, 'o' },
  { "hash-size-order", 1, NULL, OPT_HASH_SIZE_ORDER },
  { "hash-threshold", 1, NULL, 't' },
  { "aging-method", 1, NULL, 'a' },
  { "hash-mode", 0, NULL, OPT_HASH_MODE },
  { "move-pages-max", 1, NULL, 'm' },
  { "dram-count-multiple", 1, NULL, OPT_DRAM_COUNT_MULTIPLE },
  { "dram-list", 1, NULL, OPT_DRAM_LIST },
  { "pmem-list", 1, NULL, OPT_PMEM_LIST },
  { "pmem-dram-map", 1, NULL, OPT_PMEM_DRAM_MAP },
  { "filter-addr-range", 1, NULL, OPT_FILTER_ADDR_RANGE },
  { "filter-reset-intervals", 1, NULL, OPT_FILTER_RESET_INTERVALS },
  { "imc-counting", 0, NULL, OPT_IMC_COUNTING },
  { "show-only", 0, NULL, OPT_SHOW_ONLY },
  { "verbose", 0, NULL, 'v' },
  { "runtime", 1, NULL, OPT_RUNTIME },
  { "hist-max", 1, NULL, OPT_HIST_MAX },
  { "help", 0, NULL, 'h' },
  { NULL , 0, NULL, 0 }
};

int PMUMemoryOptimizer::parse_name_option(const char *option, const char *name,
                                       const char *all_names[])
{
  const char **curr;

  for (curr = all_names; *curr; curr++) {
    if (!strcmp(*curr, name))
      return curr - all_names;
  }

  fprintf(stderr, "Invalid %s, valid options: ", option);
  for (curr = all_names; *curr; curr++)
    fprintf(stderr, *(curr+1) ? "%s, " : "%s\n", *curr);
  exit(1);
}

long PMUMemoryOptimizer::parse_long_option(char *str, char **av)
{
  char *endptr;
  long val;

  val = strtol(str, &endptr, 10);
  if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
      || (errno != 0 && val == 0)) {
    goto err;
  }

  if (endptr == str)
    goto err;

  if (*endptr != '\0')
    goto err;

  return val;

err:
  printf("Invalid string %s\n", str);
  usage(av[0]);
  exit(1);
}

int PMUMemoryOptimizer::parse_filter_addr_range_option(char *str, char *av)
{
  char *p, *p2, *end_str;
  int len, pid;
  unsigned long start = -1, size = 0;

  /*
   * e.g. "1/1000:1000"
   */
  len = strlen(str);
  p = strchr(str, '/');
  if (!p)
    goto err;

  pid = (int)strtol(str, &end_str, 10);
  if (*end_str != '/')
    goto err;

  p++;
  if (p < str + len) {
    p2 = strchr(p, ':');
    if (!p2 || p == p2)
      goto err;

    start = strtol(p, &end_str, 10);
    if (*end_str != ':')
      goto err;

    p = p2 + 1;
    if (p < str + len) {
      size = strtol(p, &end_str, 10);
      if (*end_str)
        goto err;
    }
  }

  if (start != (unsigned long)-1 && size != 0) {
    arfilter_.insert_range(pid, start, size);

    struct addr_range ar;
    ar.pid = pid;
    ar.start = start;
    ar.size = size;
    hmd_config.arfilter.push_back(ar);

    return 0;
  }

err:
  printf("Invalid addr range option parameter: %s\n", str);
  usage(av);
  exit(1);
}

void PMUMemoryOptimizer::reset_arfilter(void)
{
  arfilter_.clear();

  for (auto& f: hmd_config.arfilter)
    arfilter_.insert_range(f.pid, f.start, f.size);
}

void PMUMemoryOptimizer::parse_options(int ac, char *av[])
{
  int opt;

  while ((opt = getopt_long(ac, av, "e:E:u:p:g:o:t:a:m:vh",
                            options, NULL)) != -1) {
    switch (opt) {
      case 'e':
        hmd_config.pmem_pmu_events[PET_LOCAL] = optarg;
        break;
      case 'E':
        hmd_config.pmem_pmu_events[PET_REMOTE] = optarg;
        break;
      case OPT_DRAM_LOCAL_EVENT:
        hmd_config.dram_pmu_events[PET_LOCAL] = optarg;
        break;
      case OPT_DRAM_REMOTE_EVENT:
        hmd_config.dram_pmu_events[PET_REMOTE] = optarg;
        break;
      case OPT_IMC_DRAM_READ_EVENT:
        hmd_config.imc_dram_read = optarg;
        break;
      case OPT_IMC_DRAM_WRITE_EVENT:
        hmd_config.imc_dram_write = optarg;
        break;
      case OPT_SAMPLE_PERIOD_MIN:
        hmd_config.sample_period_min = parse_long_option(optarg, av);
        break;
      case 'u':
        hmd_config.cpumask = numa_parse_cpustring(optarg);
        if (!hmd_config.cpumask) {
          fprintf(stderr, "Invalid cpu list!\n");
          exit(1);
        }
        break;
      case 'p':
        hmd_config.target_pid = parse_long_option(optarg, av);
        break;
      case 'g':
        hmd_config.granularity_order = parse_long_option(optarg, av);
        break;
      case OPT_UNIT_INTERVAL:
        hmd_config.unit_interval_ms = parse_long_option(optarg, av);
        break;
      case OPT_INTERVAL_MAX:
        hmd_config.interval_max = parse_long_option(optarg, av);
        break;
      case OPT_EXPECTED_SAMPLES:
        hmd_config.expected_samples = parse_long_option(optarg, av);
        break;
      case OPT_EXPECTED_SAMPLES_MARGIN_PERCENT:
        hmd_config.expected_samples_margin_percent =
            parse_long_option(optarg, av);
        break;
      case 'o':
        hmd_config.cmsk_cms_width_order = parse_long_option(optarg, av);
        break;
      case OPT_HASH_SIZE_ORDER:
        hmd_config.cmsk_achash_size_order =
            parse_long_option(optarg, av);
        break;
      case 't':
        hmd_config.cmsk_achash_threshold =
            parse_long_option(optarg, av);
        break;
      case 'a':
        hmd_config.aging_method = parse_name_option("aging method",
                                                    optarg, aging_method_names);
        break;
      case OPT_HASH_MODE:
        hmd_config.hash_mode = true;
        break;
      case 'm':
        hmd_config.move_pages_max = parse_long_option(optarg, av);
        break;
      case OPT_DRAM_COUNT_MULTIPLE:
        hmd_config.dram_count_multiple = parse_long_option(optarg, av);
        break;
      case OPT_DRAM_LIST:
        hmd_config.numa.numa_dram_list = optarg;
        break;
      case OPT_PMEM_LIST:
        hmd_config.numa.numa_pmem_list = optarg;
        break;
      case OPT_PMEM_DRAM_MAP:
        hmd_config.numa.pmem_dram_map = optarg;
        break;
      case OPT_FILTER_ADDR_RANGE:
        parse_filter_addr_range_option(optarg, av[0]);
        break;
      case OPT_FILTER_RESET_INTERVALS:
        hmd_config.arfilter_reset_intervals = parse_long_option(optarg, av);
        break;
      case OPT_IMC_COUNTING:
        hmd_config.imc_counting = true;
        break;
      case OPT_SHOW_ONLY:
        hmd_config.show_only = 1;
        break;
      case 'v':
        hmd_config.verbose = 1;
        break;
      case OPT_RUNTIME:
        hmd_config.runtime = parse_long_option(optarg, av);
        break;
      case OPT_HIST_MAX:
        hmd_config.cmsk_achash_hist_max = parse_long_option(optarg, av);
        break;
      case 'h':
      default:
        usage(av[0]);
        break;
    }
  }

  if (optind != ac)
    usage(av[0]);

  if (!hmd_config.show_only &&
      (1 << hmd_config.granularity_order) >
      (hmd_config.move_pages_max << PAGE_SHIFT)) {
    fprintf(stderr,
            "Too big granularity shift: %d, compared with max hot pages: %d\n",
            hmd_config.granularity_order, hmd_config.move_pages_max);
    exit(1);
  }

  if (hmd_config.verbose)
    arfilter_.show();
}

int PMUMemoryOptimizer::main(int argc, char *argv[])
{
  bool full;
  int i, arfilter_reset_intervals;
  unsigned long end = 0;

  arfilter_.clear();
  parse_options(argc, argv);
  arfilter_reset_intervals = hmd_config.arfilter_reset_intervals;

  setup_signal();

  numa_nodes_.collect(&hmd_config.numa, NULL);

  pmu_state_.init(&numa_nodes_);

  pmu_state_.open_imc_events();
  pmu_state_.open_pmem_events();
  pmu_state_.open_dram_events();

  cmsk_.achash.hash_mode = hmd_config.hash_mode;
  cmsk_.cms.width_order = hmd_config.cmsk_cms_width_order;
  cmsk_.cms.depth = 4;
  cmsk_.achash.threshold = hmd_config.cmsk_achash_threshold;
  cmsk_.achash.size_order = hmd_config.cmsk_achash_size_order;
  cmsk_.achash.hist_max = hmd_config.cmsk_achash_hist_max;

  cmsk_init(&cmsk_);

  migration_state_.init(&pmu_state_, &numa_nodes_, &arfilter_);

  if (hmd_config.runtime)
    end = rdclock() + hmd_config.runtime * NS_PER_SEC;

  while (!interrupted) {
    cmsk_age(&cmsk_);

    full = false;
    pmu_state_.begin_interval();
    migration_state_.begin_interval();

    pmu_state_.adjust_sample_period_commit();
    pmu_state_.enable_dram_events();

    for (i = 0;
         !pmu_state_.pmem_samples_enough() &&
             !interrupted && !full && i < hmd_config.interval_max;
         i++) {
      pmu_state_.begin_interval_unit();
      pmu_state_.enable_pmem_events();
      full = pmu_state_.read_pmem_samples(hmd_config.unit_interval_ms);

      if (pmu_state_.adjust_sample_period_prepare())
        break;
    }

    pmu_state_.read_imc_count();
    pmu_state_.disable_dram_events();
    pmu_state_.read_dram_count();

    numa_nodes_.collect_dram_nodes_meminfo();
    numa_nodes_.check_dram_nodes_watermark(hmd_config.dram_watermark_percent);
    pmu_state_.calc_dram_count_avg();

    cmsk_sort(&cmsk_);
    if (hmd_config.verbose)
      print_statistics();
    if (!hmd_config.show_only) {
      if (hmd_config.arfilter_reset_intervals && --arfilter_reset_intervals == 0) {
        arfilter_reset_intervals = hmd_config.arfilter_reset_intervals;
        reset_arfilter();

        if (hmd_config.verbose)
          printf("Reset address range filter\n");
      }
      migrate_pages();
    }
    if (hmd_config.runtime && rdclock() > end)
      break;
  }

  pmu_state_.close_imc_events();
  pmu_state_.close_pmem_events();
  pmu_state_.close_dram_events();

  return 0;
}

PMUMemoryOptimizer pmu_memory_optimizer;

int main(int argc, char *argv[])
{
  return pmu_memory_optimizer.main(argc, argv);
}
