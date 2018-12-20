/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 */

#ifndef AEP_PROC_STATUS_H
#define AEP_PROC_STATUS_H

#include <vector>
#include <string>
#include <unordered_map>

class ProcStatus
{
  public:
    int load(pid_t n);

    void clear();
    bool empty() const { return status_map.empty(); }

    const std::string& get_name() const { return name; }
    unsigned long get_number(std::string key) const;

  private:

    int parse_file(FILE *file);
    int parse_line(char *line);

  private:
    pid_t pid;
    std::string name;
    std::unordered_map<std::string, unsigned long> status_map;
};

#endif
// vim:set ts=2 sw=2 et:
