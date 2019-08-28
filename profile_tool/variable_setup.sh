#!/bin/bash

BASE_DIR=$(dirname $(readlink -e $0))

# sys-refs
DEFAULT_SYS_REFS_DIR=$BASE_DIR/..
SYS_REFS=$DEFAULT_SYS_REFS_DIR/sys-refs
SYS_REFS_YAML_TEMPLATE=$BASE_DIR/sys-refs-template.yaml
SYS_REFS_YAML=$BASE_DIR/sys-refs.yaml

# workload and dcpmem BW-per-GB
PERF_BW_SCRIPT=$BASE_DIR/perf_bandwidth.sh
COLD_PAGE_BW_PER_GB_LOG_LIST=cold-page-bw-per-gb-result-list
DCPMEM_HW_INFO_FILE=$BASE_DIR/dcpmem_hw_information.yaml

# kernel default source path
DEFAULT_KERNEL_SRC_DIR=/lib/modules/$(uname -r)/build
DEFAULT_KERNEL_MODULE_DIR=$BASE_DIR/../kernel_module
DEFAULT_KERNEL_MODULE=$DEFAULT_KERNEL_MODULE_DIR/ept_idle.ko

# installed perf path and pmu tools
DEFAULT_PMUTOOL_REMOTE_REPO=https://github.com/andikleen/pmu-tools.git
DEFAULT_PMUTOOL_DIR=$BASE_DIR/pmutools
PMUTOOLS_PERF=$DEFAULT_PMUTOOL_DIR/ocperf.py
PMUTOOLS_EVENT_DOWNLOAD=$DEFAULT_PMUTOOL_DIR/event_download.py
PERF=perf

get_log_dir()
{
    local pid=$1
    echo $BASE_DIR/log/pid_$pid
}

hw_get_cpu_model_id()
{
    cat /proc/cpuinfo | grep "model[[:space:]]*:" | uniq | awk '{print $3}'
}

hw_get_cpu_family_id()
{
    cat /proc/cpuinfo | grep "cpu family" | uniq | awk '{print $4}'
}

get_perf_path()
{
    if [[ -f $PMUTOOLS_PERF ]]; then
        echo $PMUTOOLS_PERF
    else
        echo $PERF
    fi

    return 0
}
