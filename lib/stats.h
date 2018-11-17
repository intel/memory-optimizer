#ifndef AEP_STATS_H
#define AEP_STATS_H

template<class M, class K>
void add_count(M& map, const K& key, int n)
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

template<class T>
inline int percent(T n, T total)
{
  return (n * 100) / total;
}

#endif
// vim:set ts=2 sw=2 et:
