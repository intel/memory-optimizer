#include <stdio.h>
#include <sys/types.h>
#include <getopt.h>

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iostream>

#include "Option.h"
#include "ProcMaps.h"
#include "ProcIdlePages.h"
#include "Migration.h"
#include "lib/debug.h"
#include "version.h"

using namespace std;

Option option;

int debug_level()
{
  return option.debug_level;
}

static const struct option opts[] = {
  {"pid",       required_argument,  NULL, 'p'},
  {"interval",  required_argument,  NULL, 'i'},
  {"loop",      required_argument,  NULL, 'l'},
  {"output",    required_argument,  NULL, 'o'},
  {"dram",      required_argument,  NULL, 'd'},
  {"hot-refs",  required_argument,  NULL, 'H'},
  {"cold-refs", required_argument,  NULL, 'c'},
  {"migrate",   required_argument,  NULL, 'm'},
  {"verbose",   required_argument,  NULL, 'v'},
  {"help",      no_argument,        NULL, 'h'},
  {NULL,        0,                  NULL, 0}
};

static void usage(char *prog)
{
  fprintf(stderr,
          "version: %s\n"
          "%s [option] ...\n"
          "    -h|--help       Show this information\n"
          "    -p|--pid        The PID to scan\n"
          "    -i|--interval   The scan interval in seconds\n"
          "    -l|--loop       The number of times to scan\n"
          "    -o|--output     The output file, defaults to refs-count-PID\n"
          "    -d|--dram       The DRAM percent, wrt. DRAM+PMEM total size\n"
          "    -H|--hot-refs   min_refs threshold for hot pages\n"
          "    -c|--cold-refs  max_refs threshold for cold pages\n"
          "    -m|--migrate    Migrate what: 0|none, 1|hot, 2|cold, 3|both\n"
          "    -v|--verbose    Show debug info\n",
          VERSION_STRING,
          prog);

  exit(0);
}

static void parse_cmdline(int argc, char *argv[])
{
  int options_index = 0;
	int opt = 0;
	const char *optstr = "hvp:i:l:o:d:H:c:m:";

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
    case 'd':
      option.dram_percent = atoi(optarg);
      break;
    case 'H':
      option.hot_min_refs = atoi(optarg);
      break;
    case 'c':
      option.cold_max_refs = atoi(optarg);
      break;
    case 'm':
      option.migrate_what = Migration::parse_migrate_name(optarg);
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

int account_refs(Migration& migration)
{
  int err;

  err = migration.walk_multi(option.nr_walks, option.interval);
  if (err)
    return err;

  ProcIdlePages::reset_sys_refs_count(migration.get_nr_walks());
  migration.count_refs();

  err = migration.save_counts(option.output_file);
  if (err)
    return err;

  return 0;
}

int migrate(Migration& migration)
{
  int err = 0;

  err = migration.migrate();
  migration.dump_task_nodes();

  return err;
}

int main(int argc, char *argv[])
{
  int err = 0;

  setlocale(LC_NUMERIC, "");

  parse_cmdline(argc, argv);

  Migration migration(option.pid);

  err = account_refs(migration);
  if (err) {
    cout << "return err " << err;
	  return err;
  }

  err = migrate(migration);

  return err;
}
