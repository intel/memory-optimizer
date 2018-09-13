#include <stdio.h>
#include <linux/limits.h>
#include <fstream>
#include <iostream>
#include <string>
#include <string.h>
#include "ProcMaps.h"


#if 1
static int parse_proc_maps(pid_t pid, std::vector<proc_maps_entry>& maps)
{
  char filename[PATH_MAX];
  proc_maps_entry e;
  char line[4096];
  int path_index;
  FILE *f;
  int ret;

  snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);

  f = fopen(filename, "r");
  if (!f) {
    perror(filename);
    return -1;
  }

  while (fgets(line, sizeof(line), f))
  {
    ret = sscanf(line, "%lx-%lx %4s %lx %x:%x %lu%*[ ]%n",
                 &e.start,
                 &e.end,
                 e.perms,
                 &e.offset,
                 &e.dev_major,
                 &e.dev_minor,
                 &e.ino,
                 &path_index);

    if (ret < 7)
    {
      fprintf(stderr, "parse failed: %d %s\n%s", ret, filename, line);
      ret = -EINVAL;
			break;
    }

    e.read     = (e.perms[0] == 'r');
    e.write    = (e.perms[1] == 'w');
    e.exec     = (e.perms[2] == 'x');
    e.mayshare = (e.perms[3] != 'p');

    e.path = line + path_index;
    e.path.pop_back();      // trim trailing '\n'

    maps.push_back(e);
  }

  fclose(f);

  return ret;
}

#else
/*
  reimplement this because the it can not handle heap area before
*/
static int parse_proc_maps(pid_t pid, std::vector<proc_maps_entry>& maps)
{
  std::ifstream input_file;
  std::string each_line, str_value[6];
  size_t each_split, split_array[7];
  int i, ret;
  char filename[PATH_MAX];
  proc_maps_entry e;

  ret = -1;
    
  snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);

  input_file.open(filename, std::ios_base::in);  
  while(std::getline(input_file, each_line))
  {
      split_array[0] = 0;

      for (i = 1; i < 6; ++i)
      {
          each_split = each_line.find_first_of(" ", split_array[i-1]);

          if (std::string::npos == each_split)
          {break;}

          //+1 for begin from next non-space item
          split_array[i] = each_split + 1;              

          //-1 because we have +1 above
          str_value[i-1] = each_line.substr(split_array[i-1], split_array[i] - split_array[i-1] - 1);
      }
      
      split_array[i] = each_line.find_first_not_of(" ", split_array[i-1]);
      if (std::string::npos != split_array[i])
      {
          str_value[i-1] = each_line.substr(split_array[i], std::string::npos);
      }
      else
      {
          str_value[i-1] = "Anonymous";
      }

      sscanf(str_value[0].c_str(), "%lx-%lx", &e.start, &e.end);
      sscanf(str_value[2].c_str(), "%lx", &e.offset);
      sscanf(str_value[3].c_str(), "%d:%d", &e.dev_major, &e.dev_minor);
      sscanf(str_value[4].c_str(), "%lu", &e.ino);
      
      e.read     = (str_value[1][0] == 'r');
      e.write    = (str_value[1][1] == 'w');
      e.exec     = (str_value[1][2] == 'x');
      e.mayshare = (str_value[1][3] != 'p');      
      e.path     = str_value[5];

      maps.push_back(e);

      ret = 0;
  }

  input_file.close();

  return ret;
}

#endif

std::vector<proc_maps_entry> ProcMaps::load(pid_t pid)
{
  std::vector<proc_maps_entry> maps;

  parse_proc_maps(pid, maps);

  return maps;
}

void ProcMaps::show(const proc_maps_entry& vma)
{
  printf("%lx-%lx %4s %08lx %02x:%02x %-8lu\t\t%s\n",
         vma.start,
         vma.end,
         vma.perms,
         vma.offset,
         vma.dev_major,
         vma.dev_minor,
         vma.ino,
         vma.path.c_str());
}

void ProcMaps::show(const std::vector<proc_maps_entry>& maps)
{
  for (const proc_maps_entry& vma: maps)
    show(vma);
}

bool ProcMaps::is_anonymous(proc_maps_entry& vma)
{
  if (!vma.path.compare("[heap]")
      || !vma.path.compare(""))
    return true;

  return false;
}
