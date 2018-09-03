#include <stdio.h>
#include <linux/limits.h>

#include "ProcMaps.h"

static int parse_proc_maps(pid_t pid, std::vector<proc_maps_entry>& maps)
{
  char filename[PATH_MAX];
  proc_maps_entry e;
  char path[4096];
	FILE *f;
	int ret;

  snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);

	f = fopen(filename, "r");
	if (!f) {
		perror(filename);
		return -1;
	}

  for (;;)
  {
    ret = fscanf(f, "%lx-%lx %4s %lx %d:%d %lu %4095s\n",
                 &e.start,
                 &e.end,
                 e.perms,
                 &e.offset,
                 &e.dev_major,
                 &e.dev_minor,
                 &e.ino,
                 path);

    if (ret == EOF) {
      if (ferror(f)) {
        perror(filename);
        ret = -2;
      } else
        ret = 0;
      break;
    }

		if (ret < 7)
    {
      fprintf(stderr, "failed to parse %s\n", filename);
      ret = -3;
			break;
    }

    e.read     = (e.perms[0] == 'r');
    e.write    = (e.perms[1] == 'w');
    e.exec     = (e.perms[2] == 'x');
    e.mayshare = (e.perms[3] != 'p');

    e.path = path;

    maps.push_back(e);
  }

  fclose(f);

  return ret;
}

std::vector<proc_maps_entry> ProcMaps::load(pid_t pid)
{
  std::vector<proc_maps_entry> maps;

  parse_proc_maps(pid, maps);

  return maps;
}
