#include "PidList.h"



int PidList::collect()
{
    return -1;
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
