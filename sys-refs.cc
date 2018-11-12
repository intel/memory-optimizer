#include <stdio.h>
#include <sys/types.h>
#include <getopt.h>

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <string.h>

#include "lib/debug.h"
#include "Option.h"
#include "ProcMaps.h"
#include "ProcIdlePages.h"
#include "Migration.h"
#include "GlobalScan.h"
#include "version.h"
#include "OptionParser.h"

using namespace std;

Option option;

int debug_level()
{
  return option.debug_level;
}

static const struct option opts[] = {
  {"interval",  required_argument,  NULL, 'i'},
  {"sleep",     required_argument,  NULL, 's'},
  {"loop",      required_argument,  NULL, 'l'},
  {"output",    required_argument,  NULL, 'o'},
  {"dram",      required_argument,  NULL, 'd'},
  {"migrate",   required_argument,  NULL, 'm'},
  {"verbose",   required_argument,  NULL, 'v'},
  {"config",    required_argument,  NULL, 'c'},
  {"help",      no_argument,        NULL, 'h'},
  {"version",   no_argument,        NULL, 'r'},
  {NULL,        0,                  NULL, 0}
};

static void usage(char *prog)
{
  fprintf(stderr,
          "%s [option] ...\n"
          "Options (order matters, the latter takes effect):\n"
          "    -h|--help       Show this information\n"
          "    -i|--interval   The scan interval in seconds\n"
          "    -s|--sleep      Seconds to sleep between scan rounds\n"
          "    -l|--loop       The number of scan rounds\n"
          "    -o|--output     The output file, defaults to refs-count\n"
          "    -d|--dram       The DRAM percent, wrt. DRAM+PMEM total size\n"
          "    -m|--migrate    Migrate what: 0|none, 1|hot, 2|cold, 3|both\n"
          "    -v|--verbose    Show debug info\n"
          "    -r|--version    Show version info\n"
          "    -c|--config     config file path name\n",
          prog);

  exit(0);
}

static void parse_cmdline(int argc, char *argv[])
{
  int options_index = 0;
  int opt = 0;
  const char *optstr = "hvri:s:l:o:d:m:c:";

  optind = 1;
  while ((opt = getopt_long(argc, argv, optstr, opts, &options_index)) != EOF) {
    switch (opt) {
    case 0:
    case 'c':
      option.config_file = optarg;
      OptionParser().Parse(option.config_file, option);
      break;
    case 's':
      option.sleep_secs = atof(optarg);
      break;
    case 'i':
      option.interval = atof(optarg);
      break;
    case 'l':
      option.nr_loops = atoi(optarg);
      break;
    case 'o':
      option.output_file = optarg;
      break;
    case 'd':
      option.dram_percent = atoi(optarg);
      break;
    case 'm':
      option.migrate_what = Option::parse_migrate_name(optarg);
      break;
    case 'v':
      ++option.debug_level;
      break;
    case 'r':
      print_version();
      exit(0);
    case 'h':
    case '?':
    default:
      usage(argv[0]);
    }
  }

}

int main(int argc, char *argv[])
{
  setlocale(LC_NUMERIC, "");

  parse_cmdline(argc, argv);

  GlobalScan gscan;

  gscan.main_loop();

  return 0;
}
