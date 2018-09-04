#include <unistd.h>
#include <iostream>
#include <stdio.h>

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

  return err;
}

int ProcIdlePages::count_refs_one(
                   std::unordered_map<unsigned long, unsigned char>& page_refs,
                   std::vector<unsigned char>& refs_count)
{
  int err = 0;

  refs_count.clear();
  refs_count.reserve(nr_walks+1);

  // TODO

  return err;
}

int ProcIdlePages:::count_refs()
{
  int err = 0;

	err = count_refs_one(page_refs_4k, refs_count_4k);
	if (err) {
		std::cerr << "count 4K page out of range" << std::endl;
	}
	err = count_refs_one(page_refs_2m, refs_count_2m);
	if (err) {
		std::cerr << "count 2M page out of range" << std::endl;
	}

  return err;
}

int ProcIdlePages::save_counts(std::string filename)
{
  int err = 0;

	FILE *file;
	file = fopen(filename, "w");
	if (file == NULL) {
		std::cerr << "open file " << filename << "failed" << std::endl;
		return 0;
	}
	fprintf(file, "%-8s %-15s %-15s\n",
                 "refs", "count_4K",
                 "count_2M");
	fprintf(file, "=========================================================\n");

	for (int i = 0; i < nr_walks; i++) {
		fprintf(file, "%-8u %-15lu %-15lu\n",
							(unsigned int)i,
							refs_4k_count[i],
							refs_2m_count[i]);
	}
	fclose(file);
	
  return err;
}
