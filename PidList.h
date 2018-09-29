#ifndef AEP_PID_LIST_H
#define AEP_PID_LIST_H

#include <vector>
#include <string>
#include <locale>

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

    int parse_one_pid(struct dirent* pid_ent);
    
    bool is_digit(const char* str_ptr) {
        
        //assumption here: in /proc the first character of
        // file name is number only happen on PIDs
        //
        return isdigit(str_ptr[0], std::locale());
    }
    
  private:
    PidSet pid_set;
    
};




#endif
