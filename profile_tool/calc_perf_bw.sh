#!/usr/bin/env bash

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/common_library.sh"

PERF_BW_EVENT=(
# CLX only, test only, with pmem support, No it doesn't work..
# cpu/event=0xbb,umask=0x1,offcore_rsp=0x7BFC007F5,name=total_read/  # pmem local/remote supported
# cpu/event=0xbb,umask=0x1,offcore_rsp=0x7BB8007F5,name=remote_read_COLD/
# cpu/event=0xbb,umask=0x1,offcore_rsp=0x7844007F5,name=local_read_HOT/

# SLX
    cpu/event=0xbb,umask=0x1,offcore_rsp=0x7bc0007f5,name=total_read/
    cpu/event=0xbb,umask=0x1,offcore_rsp=0x7b80007f5,name=remote_read_COLD/
    cpu/event=0xbb,umask=0x1,offcore_rsp=0x7840007f5,name=local_read_HOT/
)

perf=$1
target_pid=$2
perf_log=$3
run_time=$4
workload_type=
workload_type_log="$(dirname $perf_log)/cold-page-bw-per-gb-workload-type-pid-$target_pid.log"
perf_cmd="$perf stat -p $target_pid -o $perf_log "
perf_cmd_end=" -- sleep $run_time"

workload_type=$($CALC_WORKLOAD_TYPE $perf $target_pid $workload_type_log 2)

for i in ${PERF_BW_EVENT[@]}; do
    perf_cmd="$perf_cmd -e $(add_perf_event_modifier $i $workload_type) "
done

perf_cmd="$perf_cmd $perf_cmd_end"
echo $perf_cmd
echo perf_log: $perf_log
$perf_cmd
