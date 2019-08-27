#!/usr/bin/env bash

BASE_DIR=$(dirname $(readlink -e $0))

# config variable
SYS_REFS_YAML_TEMPLATE=$BASE_DIR/sys-refs-template.yaml
SYS_REFS_YAML=$BASE_DIR/sys-refs.yaml

DEFAULT_SYS_REFS_DIR=$BASE_DIR/..
SYS_REFS=$DEFAULT_SYS_REFS_DIR/sys-refs
PERF=$BASE_DIR/pmutools/ocperf.py

PERF_BW_SCRIPT=$BASE_DIR/perf_bandwidth.sh

COLD_PAGE_BW_PER_GB_LOG_LIST=cold-page-bw-per-gb-result-list
DCPMEM_HW_INFO_FILE=$BASE_DIR/dcpmem-hw-info.yaml

DEFAULT_KERNEL_SRC_DIR=/lib/modules/$(uname -r)/build
DEFAULT_KERNEL_MODULE_DIR=$BASE_DIR/../kernel_module

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
