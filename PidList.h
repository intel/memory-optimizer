#ifndef AEP_PID_LIST_H
#define AEP_PID_LIST_H

#include <vector>
#include <string>
#include <locale>

struct PidItem {
    unsigned long pid;
    unsigned long rss_anon;
    std::string   name;
};


typedef std::vector<PidItem> PidSet;

class PidList
{
  public:
    PidList(){;}
    ~PidList(){;}

    int  collect();
    PidSet& get_pidlist() { return pid_set; }

    void clear() { pid_set.clear(); }
    bool empty() { return pid_set.empty(); }

  private:

    int parse_pid(pid_t pid);
    int parse_pid_status(FILE *file);
    int parse_pid_status_line(struct PidItem &new_item, char *line_ptr);

    int get_field_name_value(char *field_ptr,
                             char **name_ptr, char** value_ptr);

    bool is_digit(const char *str_ptr) {
        // assumption here: in /proc the first character of
        // file name is number only happen on PIDs
        return isdigit(str_ptr[0], std::locale());
    }

    int save_into_pid_set(PidItem &new_pid_item);

    // Parse family here
    void parse_value_number_1(char* value_ptr,
                              unsigned long &out_value);
    void parse_value_number_with_unit(char* value_ptr,
                                      unsigned long &out_value);
    void parse_value_string_1(char* value_ptr,
                              std::string &out_value);

  private:
    PidSet pid_set;
};




#endif
