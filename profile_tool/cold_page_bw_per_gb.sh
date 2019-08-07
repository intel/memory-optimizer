#!/bin/bash

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/variable_setup.sh"

target_pid=$PID
ref_count=$REF_COUNT
page_size=$PAGE_SIZE

log_list=$COLD_PAGE_BW_PER_GB_LOG_LIST-$target_pid.log

log=$BASE_DIR/perf-bw-per-gb-$target_pid-$ref_count-$page_size.log

echo > $log
$PERF_BW_SCRIPT $PERF $target_pid $log 60

echo "" >> $log
echo "ref_count=$ref_count" >> $log
echo "page_size=$page_size" >> $log

echo $log >> $COLD_PAGE_BW_PER_GB_RESULT_LIST
