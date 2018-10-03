#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <cerrno>

#include "ProcPid.h"

std::vector<pid_t>& ProcPid::get_pids()
{
  if (pids.empty())
    collect();

  return pids;
}

int ProcPid::collect()
{
  DIR *dir;
  struct dirent *dirent;
  pid_t pid;
  int rc = 0;

  dir = opendir("/proc/");
  if (!dir)
    return -errno;

  pids.clear();

  for(;;) {
    errno = 0;
    dirent = readdir(dir);
    if (!dirent) {
      if (errno)
        rc = -errno;

      break;
    }

    if (DT_DIR != dirent->d_type)
      continue;

    if (!isdigit(dirent->d_name[0]))
      continue;

    pid = atoi(dirent->d_name);
    pids.push_back(pid);
  }

  closedir(dir);

  return rc;
}


#ifdef PID_LIST_SELF_TEST
#include "ProcStatus.h"

int main(int argc, char* argv[])
{
  ProcPid pp;
  ProcStatus ps;
  int err;

  err = pp.collect();
  if (err) {
    fprintf(stderr, "get pid list failed! err = %d\n", err);
    return err;
  }

  setlocale(LC_NUMERIC, "");

  printf("\nList all pids:\n");
  for(auto &pid : pp.get_pids()) {
    ps.load(pid);
    printf("%8u  %'15lu  %s\n",
           pid,
           ps.get_number("RssAnon"),
           ps.get_name().c_str());
    }

  printf("\nList kthreadd by name:\n");
  for(auto &pid : pp.get_pids()) {
    ps.load(pid);
    if (ps.get_name() == "kthreadd")
      printf("%8u  %'15lu  %s\n",
             pid,
             ps.get_number("RssAnon"),
             ps.get_name().c_str());
  }

  return 0;
}

#endif
