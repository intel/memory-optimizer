/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 */

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
  proc_maps.show(maps);

  return 0;
}
