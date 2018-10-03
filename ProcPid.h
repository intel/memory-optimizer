#ifndef AEP_PROC_PID_H
#define AEP_PROC_PID_H

#include <sys/types.h>
#include <vector>

class ProcPid
{
  public:
    void clear() { pids.clear(); }
    void empty() { pids.empty(); }

    int collect();
    std::vector<pid_t>& get_pids();

  private:
    std::vector<pid_t> pids;
};

#endif
// vim:set ts=2 sw=2 et:
