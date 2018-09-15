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
#include "lib/debug.h"

#define DRAM_NUMA_NODE  0
#define PMEM_NUMA_NODE  1

using namespace std;

struct task_refs_options {
  int debug_level;
  pid_t pid;
  int nr_walks;
  float interval;
  MigrateType migrate_type;

  std::string output_file;
} option;

int debug_level()
{
  return option.debug_level;
}

static const struct option opts[] = {
  {"pid",       required_argument,  NULL, 'p'},
  {"interval",  required_argument,  NULL, 'i'},
  {"loop",      required_argument,  NULL, 'l'},
  {"output",    required_argument,  NULL, 'o'},
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
          "    -v|--verbose    Show debug info\n",
          prog);

  exit(0);
}

static void parse_cmdline(int argc, char *argv[])
{
  int options_index = 0;
	int opt = 0;
	const char *optstr = "hvp:i:l:o:m:";

  option.nr_walks = 10;
  option.interval = 0.1;
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

  proc_idle_pages.count_refs();

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

  if (cold) {
    err = migration->migrate(PTE_IDLE);
    if (!err)
    err = migration->migrate(PMD_IDLE);
  }

  if (hot) {
    err = migration->migrate(PTE_ACCESSED);
    if (!err)
    err = migration->migrate(PMD_ACCESSED);
  }

  return err;
}

int main(int argc, char *argv[])
{
  int err = 0;
  ProcIdlePages proc_idle_pages;

  setlocale(LC_NUMERIC, "");

  parse_cmdline(argc, argv);
  err = account_refs(proc_idle_pages);
  if (err) {
    cout << "return err " << err;
	  return err;
  }

  err = migrate(proc_idle_pages);

  return err;
}
