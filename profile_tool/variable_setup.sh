#!/bin/bash

BASE_DIR=$(dirname $(readlink -e $0))

# config variable
SYS_REFS_YAML_TEMPLATE=$BASE_DIR/sys-refs-template.yaml
SYS_REFS_YAML=$BASE_DIR/sys-refs.yaml

#SYS_REFS=$BASE_DIR/sys-refs/sys-refs
SYS_REFS=$BASE_DIR/../sys-refs
#PERF=$BASE_DIR/pmutools/ocperf.py
PERF=perf

PERF_BW_SCRIPT=$BASE_DIR/perf_bandwidth.sh

COLD_PAGE_BW_PER_GB_LOG_LIST=$BASE_DIR/cold-page-bw-per-gb-result-list

