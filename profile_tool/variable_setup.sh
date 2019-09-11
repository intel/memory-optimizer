#!/bin/bash

BASE_DIR=$(dirname $(readlink -e $0))

# sys-refs
DEFAULT_SYS_REFS_DIR=$BASE_DIR/..
SYS_REFS=$DEFAULT_SYS_REFS_DIR/sys-refs
SYS_REFS_YAML_TEMPLATE=$BASE_DIR/sys-refs-template.yaml
SYS_REFS_YAML=$BASE_DIR/sys-refs.yaml

# workload and dcpmem MBps-per-GB
CALC_PERF_BW=$BASE_DIR/calc_perf_bw.sh
COLD_PAGE_BW_PER_GB_LOG_LIST=cold-page-bw-per-gb-result-list
DCPMEM_HW_INFO_FILE=$BASE_DIR/dcpmem_hw_information.yaml
CALC_WORKLOAD_TYPE=$BASE_DIR/calc_workload_type.rb

# kernel default source path
DEFAULT_KERNEL_SRC_DIR=/lib/modules/$(uname -r)/build
DEFAULT_KERNEL_MODULE_DIR=$BASE_DIR/../kernel_module
DEFAULT_KERNEL_MODULE=$DEFAULT_KERNEL_MODULE_DIR/kvm_ept_idle.ko

# installed perf path and pmu tools
DEFAULT_PMUTOOL_REMOTE_REPO=https://github.com/andikleen/pmu-tools.git
DEFAULT_PMUTOOL_DIR=$BASE_DIR/pmutools
PMUTOOLS_PERF=$DEFAULT_PMUTOOL_DIR/ocperf.py
PMUTOOLS_EVENT_DOWNLOAD=$DEFAULT_PMUTOOL_DIR/event_download.py
PERF=perf

