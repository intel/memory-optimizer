#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <linux/limits.h>
#include <string.h>

#include "ProcIdlePages.h"

int ProcIdlePages::walk_multi(int nr, float interval)
{
  int err;

  auto maps = proc_maps.load(pid);
  if (maps.empty())
    return -ENOENT;

  nr_walks = nr; // for use by count_refs()
  page_refs_4k.clear();
  page_refs_2m.clear();
  page_refs_1g.clear();
  
  for (int i = 0; i < nr; ++i)
  {
    err = walk();
    if (err)
      return err;

    usleep(interval * 1000000);
  }

  return 0;
}

int ProcIdlePages::walk()
{
  int err = 0;
  unsigned long read_completed;
  unsigned long parse_start,start, end;
  unsigned long parsed_end;
  /*
    step1. try begin read

    step2. got information from kernel 

    step3. iterate it, save data into maps 

    final:  end read
   */
  err = read_idlepages_begin();

  if (0 == err)
  {
    std::vector<proc_maps_entry> address_map;

    address_map = proc_maps.load(pid);

    for (size_t i = 0; i < address_map.size(); ++i)
    {
#if 1
        printf("start=%lx, end=%lx, offset=%lx, RWXS=%d%d%d%d, inode=%lu, path=%s\n",
               address_map[i].start,
               address_map[i].end,
               address_map[i].offset,
               address_map[i].read,
               address_map[i].write,
               address_map[i].exec,
               address_map[i].mayshare,
               address_map[i].ino,
               address_map[i].path.c_str());
#endif
        
        parsed_end = 0;
        end = address_map[i].end;
        start = address_map[i].start;
        parse_start = start;
        //printf("try start = %lx\n", start);
        while ((parse_start < end) && (parse_start >= start))
        {            
            err = read_idlepages(parse_start,
                                 data_buffer,
                                 sizeof(data_buffer),
                                 read_completed);
            
            if (0 == err)
            {
                parse_idlepages(parse_start,
                                data_buffer,
                                read_completed,
                                parsed_end);
                parse_start = parsed_end;
            }
            else
            {
                printf("read [%lx-%lx] for pid=%d failed, skip.\n",
                       parse_start, end, pid);
                break;
            }
        }
        //printf("\tend = %lx completed \n", parse_start);
    }
  }

  read_idlepages_end();
  
  return err;
}

int ProcIdlePages::count_refs_one(
                   std::unordered_map<unsigned long, unsigned char>& page_refs,
                   std::vector<unsigned long>& refs_count)
{
  int err = 0;
  auto iter_beigin = page_refs.begin();
  auto iter_end = page_refs.end();
  
  refs_count.clear();
  refs_count.reserve(nr_walks + 1);
 
  for (size_t i = 0; i < refs_count.capacity(); ++i)
  {
      refs_count[i] = 0;
  }
  
  for(;iter_beigin != iter_end; ++iter_beigin)
  {
      refs_count[iter_beigin->second] += 1;
  }
  
  return err;
}

int ProcIdlePages::count_refs()
{
  int err = 0;

  err = count_refs_one(page_refs_4k, refs_count_4k);
  if (err) {
    std::cerr << "count 4K page out of range" << std::endl;
    return err;
  }

  err = count_refs_one(page_refs_2m, refs_count_2m);
  if (err) {
    std::cerr << "count 2M page out of range" << std::endl;
    return err;
  }

  return err;
}

int ProcIdlePages::save_counts(std::string filename)
{
  int err = 0;

  FILE *file;
  file = fopen(filename.c_str(), "w");
  if (file == NULL) {
    std::cerr << "open file " << filename << "failed" << std::endl;
    perror(filename.c_str());
    return -1;
  }

  fprintf(file, "%-8s %-15s %-15s\n",
                "refs", "count_4K",
                "count_2M");
  fprintf(file, "=========================================================\n");
  
  for (int i = 0; i < nr_walks + 1; i++) {
    fprintf(file, "%-8d %-15lu %-15lu\n",
            i,
            refs_count_4k[i],
            refs_count_2m[i]);
  }
  fclose(file);
 
  return err;
}

int ProcIdlePages::read_idlepages_begin()
{
  char filepath[PATH_MAX];

  memset(filepath, 0, sizeof(filepath));  
  snprintf(filepath, sizeof(filepath), "/proc/%d/idle_bitmap", pid);
  
  lp_procfile = fopen(filepath, "r");
  if (NULL == lp_procfile)
  {
      printf("open proc file %s failed.\n", filepath);
      return -1;
  }

  return 0;
}

void ProcIdlePages::read_idlepages_end()
{
  if (lp_procfile)
  {
    fclose(lp_procfile);
    lp_procfile = NULL;
  }
}

#if 1
int ProcIdlePages::read_idlepages(unsigned long va_start,
                                  ProcIdleExtent* lp_idle_info,
                                  unsigned long read_size,
                                  unsigned long& completed_size)
{
  int ret_val;
  long int offset;
  size_t read_ret;
  size_t count = 1;
  
  if (!lp_procfile)
  {
    return -1;
  }

  offset = va_start / (4*KiB);
  
  ret_val = fseek(lp_procfile, offset, SEEK_SET);
  if (0 == ret_val)
  {
    read_ret = fread(lp_idle_info, read_size, count, lp_procfile);
    completed_size = read_ret;
  }
  else
  {
    completed_size = 0;
  }
  
  return ret_val;  
}
#else
int ProcIdlePages::read_idlepages(unsigned long va_start,
                                  ProcIdleExtent* lp_idle_info,
                                  unsigned long read_size,
                                  unsigned long& completed_size)
{
  int i = 0;
  
  lp_idle_info[i++] = {PTE_ACCESSED, 1};
  lp_idle_info[i++] = {PTE_ACCESSED, 2};
  lp_idle_info[i++] = {PTE_ACCESSED, 4};

  lp_idle_info[i++] = {PTE_HOLE, 1};
  lp_idle_info[i++] = {PTE_HOLE, 2};
  lp_idle_info[i++] = {PTE_HOLE, 4};

  lp_idle_info[i++] = {PTE_IDLE, 1};
  lp_idle_info[i++] = {PTE_IDLE, 2};
  lp_idle_info[i++] = {PTE_IDLE, 4};

  lp_idle_info[i++] = {PMD_ACCESSED, 1};
  lp_idle_info[i++] = {PMD_ACCESSED, 2};
  lp_idle_info[i++] = {PMD_ACCESSED, 4};

  lp_idle_info[i++] = {PMD_HOLE, 1};
  lp_idle_info[i++] = {PMD_HOLE, 2};
  lp_idle_info[i++] = {PMD_HOLE, 4};

  lp_idle_info[i++] = {PMD_IDLE, 1};
  lp_idle_info[i++] = {PMD_IDLE, 2};
  lp_idle_info[i++] = {PMD_IDLE, 4};
  
  lp_idle_info[i++] = {PUD_ACCESSED, 1};
  lp_idle_info[i++] = {PUD_ACCESSED, 2};
  lp_idle_info[i++] = {PUD_ACCESSED, 4};

  lp_idle_info[i++] = {PUD_HOLE, 1};
  lp_idle_info[i++] = {PUD_HOLE, 2};
  lp_idle_info[i++] = {PUD_HOLE, 4};

  lp_idle_info[i++] = {PUD_IDLE, 1};
  lp_idle_info[i++] = {PUD_IDLE, 2};
  lp_idle_info[i++] = {PUD_IDLE, 4};
  
  completed_size = i * sizeof(*lp_idle_info);
  
  return 0;  
}

#endif

void ProcIdlePages::update_idlepages_info(page_refs_info& info,
                                          unsigned long va,
                                          unsigned long page_size,
                                          unsigned long count)
{
    for(unsigned long i = 0; i < count; ++i)
    {
        auto find_iter = info.find(va);
        if (find_iter == info.end())
        {
            info[va] = 1;
        }
        else
        {
            info[va] += 1;
        }
        
        va += page_size;
    }
}


void ProcIdlePages::parse_idlepages(unsigned long start_va,
                                    ProcIdleExtent* lp_idle_info, unsigned long size,
                                    unsigned long& parsed_end)
{
    ProcIdleExtent* lp_end = lp_idle_info + size/sizeof(*lp_idle_info);
    
    for(; lp_idle_info != lp_end; ++lp_idle_info)
    {
        switch(lp_idle_info->type)
        {
        case PTE_ACCESSED:
            update_idlepages_info(page_refs_4k,
                                  start_va, PTE_SIZE,
                                  lp_idle_info->nr);
            start_va += PTE_SIZE * lp_idle_info->nr;
            break;
        case PMD_ACCESSED:
            update_idlepages_info(page_refs_2m,
                                  start_va, PMD_SIZE,
                                  lp_idle_info->nr);
            start_va += PMD_SIZE * lp_idle_info->nr;
            break;
        case PUD_ACCESSED:
            update_idlepages_info(page_refs_1g,
                                  start_va, PUD_SIZE,
                                  lp_idle_info->nr);
            start_va += PUD_SIZE * lp_idle_info->nr;
            break;
        case PTE_HOLE:
        case PTE_IDLE:
            start_va += PTE_SIZE * lp_idle_info->nr;
            break;
        case PMD_HOLE:
        case PMD_IDLE:
            start_va += PMD_IDLE * lp_idle_info->nr;
            break;
        case PUD_HOLE:
        case PUD_IDLE:
            start_va += PUD_SIZE * lp_idle_info->nr;
            break;
        case P4D_HOLE:
            start_va += P4D_SIZE * lp_idle_info->nr;
            break;
        default:
            break;
        }
    }

    parsed_end = start_va;
}
