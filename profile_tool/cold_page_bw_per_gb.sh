#!/usr/bin/env bash

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/variable_setup.sh"

target_pid=$TARGET_PIDS
ref_count=$REF_COUNT
page_size=$PAGE_SIZE
page_count=$PAGE_COUNT

log_list=$(get_log_dir $target_pid)/$COLD_PAGE_BW_PER_GB_LOG_LIST-$target_pid.log
log=$(get_log_dir $target_pid)/perf-bw-per-gb-$target_pid-$ref_count-$page_size.log


echo > $log
$PERF_BW_SCRIPT $PERF $target_pid $log 30

cat >> $log <<EOF
ref_count=$ref_count
page_size=$page_size
page_count=$page_count
EOF

echo $log >> $log_list
