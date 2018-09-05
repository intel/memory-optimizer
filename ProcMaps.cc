#include <stdio.h>
#include <linux/limits.h>
#include <fstream>
#include <iostream>
#include <string>
#include <string.h>
#include "ProcMaps.h"


#if 0
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

          if (-1 == each_split)
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
      sscanf(str_value[5].c_str(), "%s", filename);

      e.read     = (str_value[1][0] == 'r');
      e.write    = (str_value[1][1] == 'w');
      e.exec     = (str_value[1][2] == 'x');
      e.mayshare = (str_value[1][3] != 'p');      
      e.path     = filename;

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
