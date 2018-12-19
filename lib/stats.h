#ifndef AEP_STATS_H
#define AEP_STATS_H

#include <sys/time.h>

template<class M, class K, class V>
void add_count(M& map, const K& key, V&& n)
{
  auto search = map.find(key);

  if (search != map.end())
    search->second += n;
  else
    map[key] = n;
}

template<class M, class K>
void inc_count(M& map, const K& key)
{
  add_count(map, key, 1);
}

template<class M, class K, class V>
int find_map(M& map, const K& key, V&& value)
{
  auto search = map.find(key);
  if (search != map.end()) {
    value = search->second;
    return 1;
  }

  return 0;
}

template<class T>
inline int percent(T n, T total)
{
  return (n * 100) / total;
}

static inline float tv_secs(struct timeval& t1, struct timeval& t2)
{
  return  (t2.tv_sec  - t1.tv_sec) +
          (t2.tv_usec - t1.tv_usec) * 0.000001;
}


#endif
// vim:set ts=2 sw=2 et:
