#ifndef AEP_STATS_H
#define AEP_STATS_H

template<class M, class K>
void inc_count(M& map, const K& key)
{
    auto search = map.find(key);

    if (search != map.end())
	    ++search->second;
    else
        map[key] = 1;
}

template<class T>
inline int percent(T n, T total)
{
  return (n * 100) / total;
}

#endif
// vim:set ts=2 sw=2 et:
