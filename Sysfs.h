/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 */

#ifndef AEP_SYSFS_H
#define AEP_SYSFS_H

#include <string>
#include <vector>
#include <unordered_map>

typedef std::unordered_map<int, int> IntMap;

class Sysfs
{
  public:
    int hugetlb(int nid, std::string name);
    void load_hugetlb();

    bool file_exists(char path[]);
    int read_int(std::string dir, std::string name);

  private:
    std::vector<std::unordered_map<std::string, int>> hugetlb_map;
};

#endif
// vim:set ts=2 sw=2 et:
