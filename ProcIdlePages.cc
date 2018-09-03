#include <unistd.h>

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

int ProcIdlePages::count_refs()
{
  int err = 0;

  count_refs_one(page_refs_4k, refs_count_4k);
  count_refs_one(page_refs_2m, refs_count_2m);

  // TODO

  return err;
}

int ProcIdlePages::save_counts(std::string filename)
{
  int err = 0;

  // TODO

  return err;
}
