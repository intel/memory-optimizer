#include "PidList.h"
#include <dirent.h>
#include <string>
#include <cstdlib>
#include <string.h>

int PidList::collect()
{
    DIR *dir;
    struct dirent *dirent;
    int ret_val = 0;

    dir = opendir("/proc/");
    if (!dir)
        return -ENOENT;

    for(;;) {
        errno = 0;
        ret_val = 0;
        dirent = readdir(dir);
        if (!dirent) {
            if (0 != errno)
                ret_val = errno;

            break;
        }

        if (DT_DIR != dirent->d_type
            || !is_digit(dirent->d_name))
            continue;

        ret_val = parse_one_pid(dirent);
        if (ret_val < 0) {
            fprintf(stderr, "parse_one_pid failed. name: %s err: %d\n",
                    dirent->d_name, ret_val);
            break;
        }
    }

    closedir(dir);

    return ret_val;
}


int PidList::parse_one_pid(struct dirent* proc_ent)
{
    int ret_val;
    FILE *file;
    char filename[PATH_MAX];

    snprintf(filename, sizeof(filename), "/proc/%s/status", proc_ent->d_name);
    file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "open %s failed\n", filename);
        return errno;
    }

    ret_val = do_parse_one_pid(file, proc_ent);

    fclose(file);

    return ret_val;
}

int PidList::do_parse_one_pid(FILE *file, struct dirent* proc_ent)
{
    int ret_val;
    char line[4096];
    struct PidItem new_pid_item = {};

    while(fgets(line, sizeof(line), file)) {
        ret_val = do_parse_one_line(new_pid_item, proc_ent, line);
        if (ret_val < 0)
            break;
    }

    if (ret_val >= 0)
        ret_val = save_into_pid_set(new_pid_item);

    return ret_val;
}

int PidList::do_parse_one_line(struct PidItem &new_pid_item,
                               struct dirent* proc_ent, char* line_ptr)
{
    int ret_val;
    char* name_ptr;
    char* value_ptr;

    ret_val = get_field_name(line_ptr, &name_ptr, &value_ptr);
    if (ret_val < 0)
        return ret_val;

    // begin here

    new_pid_item.pid = strtoul(proc_ent->d_name, NULL, 0);

    if (!strcmp("Name", name_ptr)) {
        parse_value_string_1(value_ptr, new_pid_item.name);
    }

    if (!strcmp("RssAnon", name_ptr)) {
        parse_value_number_with_unit(value_ptr, new_pid_item.rss_anon);
    }

    // add more here if necessary
    // end here

    return ret_val;
}

int PidList::get_field_name(char *field_ptr,
                            char **name_ptr, char **value_ptr)
{
    int split_index = -1;

    sscanf(field_ptr, "%*[^:]%n", &split_index);
    if (split_index > 0) {
        field_ptr[split_index] = 0;

        *name_ptr = field_ptr;
        *value_ptr = field_ptr + split_index + 1;

        return 0;
    }

    return -PARSE_FIELD_NAME_VALUE_FAILED;
}

int PidList::save_into_pid_set(PidItem& new_pid_item)
{
    try {
        pid_set.push_back(new_pid_item);
    } catch (std::bad_alloc& e) {
        return -ENOMEM;
    }

    return 0;
}

void PidList::parse_value_number_with_unit(char* value_ptr,
                                           unsigned long &out_value)
{
    int scan_ret;
    int num_value;
    char unit[16];

    scan_ret = sscanf(value_ptr, "%*[\t]%*[ ]%d %s",
                      &num_value,
                      unit);
    if (scan_ret < 1) {
        out_value = 0;
        return;
    }

    out_value = num_value;
    if (!strcmp(unit, "kB"))
        out_value <<= 10;

    return;
}

void PidList::parse_value_string_1(char* value_ptr,
                                   std::string &out_value)
{
    int name_index = -1;

    sscanf(value_ptr, "\t%n",
           &name_index);

    if (name_index <= 0) {
        out_value = "";
        return;
    }

    out_value = value_ptr + name_index;
    out_value.pop_back(); // trim '\n'

    return;
}
#ifdef PID_LIST_SELF_TEST

int main(int argc, char* argv[])
{
    PidList pl;
    int ret_val;

    ret_val = pl.collect();
    if (ret_val) {
        fprintf(stderr, "get pid list failed! err = %d\n", ret_val);
    }

    printf("\nList all pids:\n");
    for(auto &item : pl.get_pidlist()) {
        printf("PID: %lu name: %s RssAnon: %lu\n",
               item.pid,
               item.name.c_str(),
               item.rss_anon);
    }

    printf("\nList kthreadd by name:\n");
    for(auto &item : pl.get_pidlist()) {
        if (pl.is_name(item, "kthreadd"))
            printf("PID: %lu name: %s RssAnon: %lu\n",
                   item.pid,
                   item.name.c_str(),
                   item.rss_anon);
    }

    printf("\nList pids only have RssAnon:\n");
    for(auto &item : pl.get_pidlist()) {
        if (pl.is_have_rss_anon(item))
            printf("PID: %lu name: %s RssAnon: %lu\n",
                   item.pid,
                   item.name.c_str(),
                   item.rss_anon);
    }

    return 0;
}

#endif
