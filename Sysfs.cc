/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <numa.h>
#include "Sysfs.h"

#include <iostream>
#include <fstream>

bool Sysfs::file_exists(char path[])
{
  struct stat buf;

  return stat(path, &buf) == 0;
}

int Sysfs::read_int(std::string dir, std::string name)
{
  std::ifstream infile(dir + '/' + name);
  int val;
  infile >> val;
  return val;
}

void Sysfs::load_hugetlb()
{
  int max_node = numa_max_node();
  char path[100];

  hugetlb_map.resize(max_node + 1);

  for (int i = 0; i < max_node; ++i)
  {
    snprintf(path, sizeof(path),
             "/sys/devices/system/node/node%d/hugepages/hugepages-2048kB", i);

    if (!file_exists(path))
      continue;

#define read_hugetlb(name)  hugetlb_map[i][name] = read_int(path, name)
    read_hugetlb("nr_hugepages");
    read_hugetlb("free_hugepages");
    read_hugetlb("surplus_hugepages");
#undef read_hugetlb
  }
}

int Sysfs::hugetlb(int nid, std::string name)
{
  if (hugetlb_map.empty())
    load_hugetlb();
  return hugetlb_map.at(nid).at(name);
}
