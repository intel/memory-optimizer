#include <sys/types.h>
#include <getopt.h>

#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "ProcMaps.h"
#include "ProcIdlePages.h"

struct task_refs_options {
  int debug_level;
  pid_t pid;
  int nr_walks;
  float interval;
  std::string output_file;
} option;

static const struct option opts[] = {
  {"pid",       required_argument,  NULL, 'p'},
  {"interval",  required_argument,  NULL, 'i'},
  {"loop",      required_argument,  NULL, 'l'},
  {"output",    required_argument,  NULL, 'o'},
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
    option.output_file = "refs-count-" + option.pid;
}


int account_refs()
{
  int err;
  auto proc_idle_pages = std::make_unique<ProcIdlePages>();

  proc_idle_pages->set_pid(option.pid);

  err = proc_idle_pages->walk_multi(option.nr_walks, option.interval);
  if (err)
    return err;

  err = proc_idle_pages->count_refs();
  if (err)
    return err;

  err = proc_idle_pages->save_counts(option.output_file);
  if (err)
    return err;

  return 0;
}

int main(int argc, char *argv[])
{
  int err;

  parse_cmdline(argc, argv);
  err = account_refs();

  return err;
}
