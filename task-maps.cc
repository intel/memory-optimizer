#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

#include "ProcMaps.h"

int main(int argc, char *argv[])
{
  pid_t pid;
  ProcMaps proc_maps;

  if (argc == 1)
    pid = getpid();
  else
    pid = atoi(argv[1]);

  auto maps = proc_maps.load(pid);

  for (const proc_maps_entry& e: maps)
    printf("%lx-%lx %4s %08lx %02x:%02x %-8lu\t\t%s\n",
                 e.start,
                 e.end,
                 e.perms,
                 e.offset,
                 e.dev_major,
                 e.dev_minor,
                 e.ino,
                 e.path.c_str());

  return 0;
}
