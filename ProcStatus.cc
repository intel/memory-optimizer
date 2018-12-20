/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * Authors: Fengguang Wu <fengguang.wu@intel.com>
 */

#include <linux/limits.h>
#include <string.h>
#include <string>

#include "ProcStatus.h"

void ProcStatus::clear()
{
  status_map.clear();
  name.clear();
  pid = 0;
}

unsigned long ProcStatus::get_number(std::string key) const
{
  auto it = status_map.find(key);

  if (it != status_map.end())
    return it->second;
  else
    return 0; // kthreadd does not has RssAnon
}

int ProcStatus::load(pid_t n)
{
  int rc;
  FILE *file;
  char filename[PATH_MAX];

  pid = n;

  snprintf(filename, sizeof(filename), "/proc/%d/status", pid);
  file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "open %s failed\n", filename);
    return errno;
  }

  rc = parse_file(file);

  fclose(file);

  return rc;
}

int ProcStatus::parse_file(FILE *file)
{
  int rc;
  char line[4096];

  while (fgets(line, sizeof(line), file)) {
    rc = parse_line(line);
    if (rc < 0)
      return rc;
  }

  return 0;
}

int ProcStatus::parse_line(char* line)
{
  char* val;

  val = strstr(line, ":\t");
  if (!val) {
    printf("failed to parse status line:\n%s", line);
    return -EINVAL;
  }

  *val++ = '\0';
  while (*++val == ' ')
    ;

  if (!strcmp("Name", line)) {
    name = val;
    name.pop_back(); // remove trailing '\n'
    return 0;
  }

  if (isdigit(val[0])) {
    status_map[line] = atoi(val);
    // printf("%s = %lu\n", line, status_map[line]);
    return 0;
  }

  return 0;
}
