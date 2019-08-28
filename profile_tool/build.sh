#!/bin/bash

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/variable_setup.sh"

#runtime variable


build_kernel_module()
{
    local kernel_src_dir=$KERNEL_SRC_PATH
    local result=

    echo "[Task] Build kernel module..."

    if [[ -z $kernel_src_dir ]]; then
       kernel_src_dir=$DEFAULT_KERNEL_SRC_DIR
    fi

    make -C$DEFAULT_KERNEL_MODULE_DIR \
         KERNEL_SRC_DIR=$kernel_src_dir \
         MODULE_DIR=$DEFAULT_KERNEL_MODULE_DIR clean default

    result=$?
    if (( $result != 0 )); then
        echo "[error] Failed to build kernel module: $result"
        exit $result
    fi

    echo "[Success] Kernel module building completed."
}

build_memory_optimizer()
{
    local result=

    echo "[Task] Build memory optimizer..."

    make -C$DEFAULT_SYS_REFS_DIR clean sys-refs
    result=$?
    if (( $result != 0 )); then
        echo "[error] Failed to build memory optimizer: $result"
        exit $result
    fi

    echo "[Success] Memory optimizer building completed."
}

check_binary_tool_dependence()
{
    local exist=
    local error=0
    local dependence=(
        ruby
        numactl
        taskset
        gnuplot
        stdbuf
        migratepages
        awk
        sed
        grep
        uniq
        uname
        make
        insmod
        rmmod
        sleep
        perf
        git
        sleep
        lsmod
        basename
    )

    echo "[Task] Check binary tools dependence..."
    for i in "${dependence[@]}"; do
        exist=$(which $i 2>&1)
        exist=$?
        if (( $exist != 0 )); then
            echo "[error] Not found tool: $i"
            error=1
        fi
    done

    if (( $error == 1 )); then
        exit $error
    fi

    echo "[Success] Dependence checking completed."
}

check_perf_event_exist()
{
    local perf=$1
    local event_list=$2
    local perf_ret=0
    local check_cmd="$perf stat "
    local sub_sleep=0

    for i in $event_list; do
        check_cmd="$check_cmd -e $i "
    done
    sleep 5 &
    sub_sleep=$!
    check_cmd="$check_cmd -p $sub_sleep -- sleep 1"
    $check_cmd > /dev/null 2>&1
    perf_ret=$?
    if (( $perf_ret != 0 )); then
        return 1
    fi

    return 0
}

download_pmu_tools_event_files()
{
    local cmd_ret=0

cat <<EOF

Downloading pmu-tools event files:
EOF

    if [[ ! -f $PMUTOOLS_EVENT_DOWNLOAD ]]; then
        cat <<EOF
Can't find pmu tools event download script: $PMUTOOLS_EVENT_DOWNLOAD at $DEFAULT_PMUTOOL_DIR,
you may need to vist $DEFAULT_PMUTOOL_REMOTE_REPO for more information.
EOF
        exit -1
    fi

    $PMUTOOLS_EVENT_DOWNLOAD
    cmd_ret=$?
    if (( $cmd_ret != 0 )); then
        cat <<EOF
Failed to download event files for pmu tools, you may need to check network or proxy settings,
or rm the $DEFAULT_PMUTOOL_DIR folder and try again.
EOF
        exit -1
    fi

    return 0
}

download_pmu_tools()
{
    local cmd_ret=0

    if [[ -f $PMUTOOLS_PERF ]]; then
        cat <<EOF
Downloading pmu-tools: [ Skipped ]
Because it already existed at $DEFAULT_PMUTOOL_DIR, you can remove it and run this script to download it again.
EOF
        return 0
    fi

    mkdir -p $DEFAULT_PMUTOOL_DIR
    git clone $DEFAULT_PMUTOOL_REMOTE_REPO $DEFAULT_PMUTOOL_DIR
    cmd_ret=$?
    if (( $cmd_ret != 0 )); then
        cat <<EOF
Failed to get pmu tools from $DEFAULT_PMUTOOL_REMOTE_REPO, please check your network or proxy settings.
EOF
        exit -1
    fi

    return 0
}


check_perf_and_pmutool()
{
    local event_not_found=0
    local raw_event_not_found=0

    local event_list=(
        # :0:2
        offcore_response.all_pf_data_rd.any_response
        offcore_response.all_data_rd.any_response

        # :2:2
        l2_rqsts.all_pf
        l2_rqsts.all_demand_data_rd

        # :4:3
        cpu/event=0xbb,umask=0x1,offcore_rsp=0x7bc0007f5,name=total_read/
        cpu/event=0xbb,umask=0x1,offcore_rsp=0x7b80007f5,name=remote_read_COLD/
        cpu/event=0xbb,umask=0x1,offcore_rsp=0x7840007f5,name=local_read_HOT/
    )

    echo "[Task] Check the perf or pmu tools..."

    check_perf_event_exist $PERF "${event_list[*]:0:2}"
    event_not_found=$(($event_not_found + $?))
    check_perf_event_exist $PERF "${event_list[*]:2:2}"
    event_not_found=$(($event_not_found + $?))

    check_perf_event_exist $PERF "${event_list[*]:4:3}"
    raw_event_not_found=$(($raw_event_nGot_found + $?))

    if (( $event_not_found > 1 || $raw_event_not_found > 0 )); then
        download_pmu_tools
        download_pmu_tools_event_files
    else
        cat <<EOF
Skipped download pmu-tools because the installed perf at $(which $PERF) is good enough for profiling.
EOF
    fi

    echo "[Success] Check the perf or pmu tools completed. "
    return 0
}

check_binary_tool_dependence
build_kernel_module
build_memory_optimizer
check_perf_and_pmutool

cat <<EOF
Build completed successfully.
Please run profile.sh without parameters to get more information.
EOF
