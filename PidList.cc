#include "PidList.h"
#include <dirent.h>


int PidList::collect()
{
    DIR *dir;
    struct dirent *dirent;
    int ret_val = 0;
    
    dir = opendir("/proc/");
    if (!dir)
        return -ENOENT;
    
    for(;;) {
        
        dirent = readdir(dir);        
        if (!dirent)
            break;

        if (is_digit(dirent->d_name)) {
            ret_val = parse_one_pid(dirent);
        }           
    }

    closedir(dir);
    
    return ret_val;
}


int PidList::parse_one_pid(struct dirent* pid_ent)
{
    printf("name:%s\n", pid_ent->d_name);
    return 0;
}

#ifdef PID_LIST_SELF_TEST

int main(int argc, char* argv[])
{
    PidList pl;

    if (!pl.collect()) {
        for(auto &item : pl.get_pidlist()) {
            printf("PID = %lu, name:%s RssAnon = %lu\n",
                   item.pid,
                   item.name.c_str(),
                   item.RssAnon);
        }
    } else {
        fprintf(stderr, "get pid list failed!\n");
    }

    return 0;
}

#endif
