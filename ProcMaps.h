#ifndef AEP_PROC_MAPS_H
#define AEP_PROC_MAPS_H

// interface to /proc/PID/maps

#include <sys/types.h>

#include <string>
#include <vector>

struct proc_maps_entry
{
	unsigned long start;
	unsigned long end;

  char perms[5];
	bool read;
	bool write;
	bool exec;
	bool mayshare;

	unsigned long offset;
	int dev_major;
	int dev_minor;
	unsigned long ino;

  std::string path;
};

class ProcMaps
{
  public:
    std::vector<proc_maps_entry> load(pid_t pid);
    void show(const std::vector<proc_maps_entry>& maps);
    void show(const proc_maps_entry &vma);
    bool is_anonymous(proc_maps_entry& vma);
};

#endif
// vim:set ts=2 sw=2 et:
