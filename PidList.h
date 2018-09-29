#ifndef AEP_PID_LIST_H
#define AEP_PID_LIST_H

#include <vector>
#include <string>

struct PidItem {
    unsigned long pid;
    
    unsigned long RssAnon;
    std::string   name;
};


typedef std::vector<PidItem> PidSet;

class PidList
{
  public: 
    PidList(){;}
    ~PidList(){;}

    int collect();
    
    PidSet& get_pidlist() {
      return pid_set;
    }
    
  private:
    PidSet pid_set;
    
};




#endif
