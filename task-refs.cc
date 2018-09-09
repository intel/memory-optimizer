#include <stdio.h>
#include <sys/types.h>
#include <getopt.h>

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iostream>

#include "ProcMaps.h"
#include "ProcIdlePages.h"
#include "Migration.h"

#define DRAM_NUMA_NODE  0
#define PMEM_NUMA_NODE  1

using namespace std;

struct task_refs_options {
  int debug_level;
  pid_t pid;
  int nr_walks;
  int samples_percent; // samples percent for migration
  int pages_percent;   // pages percent for migration
  int hot_node;        // the numa node for hot pages
  int cold_node;       // the numa node for cold pages
  float interval;
  MigrateType migrate_type;

  std::string output_file;
} option;

static const struct option opts[] = {
  {"pid",       required_argument,  NULL, 'p'},
  {"interval",  required_argument,  NULL, 'i'},
  {"loop",      required_argument,  NULL, 'l'},
  {"output",    required_argument,  NULL, 'o'},
  {"samples",   required_argument,  NULL, 's'},
  {"pages",     required_argument,  NULL, 'g'},
  {"hotnode",   required_argument,  NULL, 'n'},
  {"coldnode",  required_argument,  NULL, 'd'},
  {"mtype",     required_argument,  NULL, 'm'},
  {"verbose",   required_argument,  NULL, 'v'},
  {"help",      no_argument,        NULL, 'h'},
  {NULL,        0,                  NULL, 0}
};

static void usage(char *prog)
{
  fprintf(stderr,
          "%s [option] ...\n"
          "    -h|--help       Show this information\n"
          "    -p|--pid        The PID to scan\n"
          "    -i|--interval   The scan interval in seconds\n"
          "    -l|--loop       The number of times to scan\n"
          "    -o|--output     The output file, defaults to refs-count-PID\n"
          "    -m|--mtype      Migrate which types of pages; "
                               "0 for hot, 1 for cold, 2 for all\n"
          "    -s|--samples    Set the samples_percent of migration policy\n"
          "    -g|--pages      Set the pages_percent of migration policy\n"
          "    -n|--hotnode    Set the numa node for hot pages\n"
          "    -d|--coldnode   Set the numa node for cold pages\n"
          "    -v|--verbose    Show debug info\n",
          prog);

  exit(0);
}

static void parse_cmdline(int argc, char *argv[])
{
  int options_index = 0;
	int opt = 0;
	const char *optstr = "hvp:i:l:o:";

  option.nr_walks = 10;
  option.interval = 0.1;
  option.hot_node = DRAM_NUMA_NODE;
  option.cold_node = PMEM_NUMA_NODE;
  // migrate the hot pages
  option.migrate_type = MIGRATE_HOT_PAGES;

  while ((opt = getopt_long(argc, argv, optstr, opts, &options_index)) != EOF) {
    switch (opt) {
    case 0:
      break;
    case 'p':
      option.pid = atoi(optarg);
      break;
    case 'i':
      option.interval = atof(optarg);
      break;
    case 'l':
      option.nr_walks = atoi(optarg);
      break;
    case 'o':
      option.output_file = optarg;
      break;
    case 'm':
      option.migrate_type = (MigrateType)atoi(optarg);
      break;
    case 's':
      option.samples_percent = atoi(optarg);
      break;
    case 'g':
      option.pages_percent = atoi(optarg);
      break;
    case 'n':
      option.hot_node = atoi(optarg);
      break;
    case 'd':
      option.cold_node = atoi(optarg);
      break;
    case 'v':
      ++option.debug_level;
      break;
    case 'h':
    case '?':
    default:
      usage(argv[0]);
    }
  }

  if (!option.pid)
    usage(argv[0]);

  if (option.output_file.empty())
    option.output_file = "refs-count-" + std::to_string(option.pid);
}


int account_refs(ProcIdlePages& proc_idle_pages)
{
  int err;

  proc_idle_pages.set_pid(option.pid);

  err = proc_idle_pages.walk_multi(option.nr_walks, option.interval);
  if (err)
    return err;

  err = proc_idle_pages.count_refs();
  if (err)
    return err;

  err = proc_idle_pages.save_counts(option.output_file);
  if (err)
    return err;

  return 0;
}

int migrate(ProcIdlePages& proc_idle_pages)
{
  int err = 0;
  bool hot = false, cold = false;
  auto migration = std::make_unique<Migration>(proc_idle_pages);

  // for example:
  // migrate the top 20% frequency of being accessed to dram node
  // migrate not more than 20% number of hot pages to dram node
  switch (option.migrate_type) {
    case MIGRATE_HOT_COLD_PAGES:
      hot = cold = true;
      break;
    case MIGRATE_HOT_PAGES:
      hot = true;
      break;
    case MIGRATE_COLD_PAGES:
      cold = true;
      break;
    default:
      hot = cold = true;
      break;
  }

  if (hot) {
    migration->set_policy(option.samples_percent,
                          option.pages_percent,
                          option.hot_node,
                          PTE_ACCESSED);

    err = migration->migrate(PTE_ACCESSED);
  }

  if (cold) {
    migration->set_policy(option.samples_percent,
                          option.pages_percent,
                          option.cold_node,
                          PTE_IDLE);

    err = migration->migrate(PTE_IDLE);
  }

  return err;
}

int main(int argc, char *argv[])
{
  int err = 0;
  ProcIdlePages proc_idle_pages;

  parse_cmdline(argc, argv);
  err = account_refs(proc_idle_pages);
  if (err) {
    cout << "return err " << err;
	  return err;
  }

  err = migrate(proc_idle_pages);

  return err;
}
