#ifndef AEP_PROC_VMSTAT_H
#define AEP_PROC_VMSTAT_H

// interface to /proc/vmstat and /sys/devices/system/node/node*/vmstat

#include <string>
#include <vector>
#include <unordered_map>

typedef std::unordered_map<std::string, unsigned long> vmstat_map;

class ProcVmstat
{
  public:
    int load_vmstat();
    int load_numa_vmstat();

    unsigned long vmstat(std::string name);
    unsigned long vmstat(int nid, std::string name);

  private:
    vmstat_map __load_vmstat(const char *path);

  private:
    std::vector<vmstat_map> numa_vmstat;
    vmstat_map proc_vmstat;
};

#endif
// vim:set ts=2 sw=2 et:
