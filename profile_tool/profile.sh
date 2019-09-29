#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
#

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/common_library.sh"

# parameter variable
target_pid=
dram_percent=25
hot_node=
cold_node=
run_time=1200
dcpmem_dimm_size=
dcpmem_combine_type=

# runtime variable
sys_refs_pid=
perf_pid=
sys_refs_log=
sys_refs_progressive_profile_log=
perf_log=
cold_page_bw_per_gb_log_list=
target_pid_cpu_affinity=
dcpmem_bw_per_gb=
workload_type=
numa_balance_setting=

# sub scripts
PARSER_PERF_IPC=$BASE_DIR/parser_perf_ipc.rb
CALC_PERF_IPC=$BASE_DIR/calc_perf_ipc.rb

PARSER_PERF_BW=$BASE_DIR/parser_perf_bw.rb

PARSER_SYSREFS_RATIO=$BASE_DIR/parser_sysrefs_ratio.rb

CALC_COLD_PAGE_BW_PER_GB=$BASE_DIR/calc_cold_page_bw_per_gb.sh
PARSER_COLD_PAGE_BW_PER_GB=$BASE_DIR/parser_bw_per_gb.rb

CALC_DCPMEM_BW_PER_GB=$BASE_DIR/calc_dcpmem_bw_per_gb.rb

PARSER_SYSREFS_HOTNESS_DRIFITING=$BASE_DIR/parser_sysrefs_hotness_drifting.rb

# const
SYS_REFS_RUNTIME=1200
PERF_IPC_RUN_TIME=60
DCPMEM_BW_PER_GB_RUN_TIME=60

usage() {
    cat <<EOF
usage: $0:
       -p target PID.
       -d dram percent for target PID.
       -h optional, NUMA node id for HOT pages.
       -c optional, NUMA node id for COLD pages.
       -t Run time, in second unit.
       -m DCPMEM DIMM type: 128 256 512.
       -i DCPMEM configuration: single 211 221 222.

For example:
The AEP in system combined by 128GB X 4 in 2-1-1 configuration,
the target pid is 100, we want to leave 25% hot pages to NUMA node 0,
leave remain cold pages to NUMA node 1, run total 120 seconds in profiling
stage:

$0 -p 100 -d 25 -h 0 -c 1 -m 128 -i 211 -t 120

EOF
}

parse_parameter() {
    while getopts ":p:d:h:c:t:s:m:i:" optname; do
        case "$optname" in
            "p")
                target_pid=$OPTARG
                ;;
            "d")
                dram_percent=$OPTARG
                ;;
            "h")
                hot_node=$OPTARG
                ;;
            "c")
                cold_node=$OPTARG
                ;;
            "t")
                run_time=$OPTARG
                ;;
            "m")
                dcpmem_dimm_size=$OPTARG
                ;;
            "i")
                dcpmem_combine_type=$OPTARG
                ;;
            ":")
                echo "no value for option $OPTARG"
                ;;
            "?")
                echo "WARNING: Ingore the unknow parameters"
                ;;
        esac
    done
}

check_parameter() {
    local will_exit=0
    if [[ -z $hot_node ]]; then
        echo "Please use -h to indicate NUMA node for HOT pages."
        will_exit=1
    fi

    if [[ -z $cold_node ]]; then
        echo "Please use -c to indicate NUMA node for COLD pages."
        will_exit=1
    fi

    if [[ -z $target_pid ]]; then
        echo "Please use -p to indicate target PID."
        will_exit=1
    fi

    if [[ -z $dcpmem_dimm_size ]]; then
        echo "Please use -m to indicate the DCPMEM DIMM size."
        will_exit=1
    fi

    if [[ -z $dcpmem_dimm_size ]]; then
        echo "Please use -i to indicate the DCPMEM configuration."
        will_exit=1
    fi

    if [[ $will_exit -eq 1 ]]; then
        usage
        exit 1
    fi

cat<<EOF
parameter dump:
  Target Pid: $target_pid
  DRAM percent: $dram_percent
  Run time: $run_time
  Hot node: $hot_node
  Cold node: $cold_node
  DCPMEM dimm size: $dcpmem_dimm_size
  DCPMEM configuration: $dcpmem_combine_type
EOF

    log_dir=$(get_log_dir $target_pid)
    sys_refs_log=$log_dir/sys-refs-$target_pid-$dram_percent.log
    perf_log=$log_dir/perf-$target_pid-$dram_percent.log
    sys_refs_progressive_profile_log=$log_dir/sys-refs-progress-profile-$target_pid.log
    cold_page_bw_per_gb_log_list=$log_dir/$COLD_PAGE_BW_PER_GB_LOG_LIST-$target_pid.log
    create_log_dir $log_dir
}

prepare_sys_refs() {
    local pid=$1
    if  [[ ! -e $SYS_REFS_YAML_TEMPLATE ]]; then
        echo "Error: No $SYS_REFS_YAML_TEMPLATE file"
        exit 10
    fi

    cat $SYS_REFS_YAML_TEMPLATE > $SYS_REFS_YAML

    # NUMA part
    cat >> $SYS_REFS_YAML <<EOF
  numa_nodes:
    $hot_node:
      type: DRAM
      demote_to: $cold_node
    $cold_node:
      type: PMEM
      demote_to: $hot_node
EOF
    # policy part
    cat >> $SYS_REFS_YAML <<EOF

policies:
  - pid: $pid
    dump_distribution: true

EOF
}

run_sys_refs() {
    echo "sys_refs_log: $sys_refs_log"
    stdbuf -oL $SYS_REFS -d $dram_percent -c $SYS_REFS_YAML > $sys_refs_log 2>&1 &
    sys_refs_pid=$(pidof sys-refs)
    echo "sys-refs pid: $sys_refs_pid"
}


parse_perf_log() {
    if [[ ! -f $perf_log ]]; then
        echo "ERROR: No $perf_log file"
        exit -1
    fi

    cat $perf_log
    $PARSER_PERF_BW < $perf_log
}

parse_sys_refs_log() {

    [[ -f $sys_refs_log ]] || return 0;


    echo ""
    $PARSER_SYSREFS_RATIO < $sys_refs_log
}

cpu_to_node() {
    local cpu=cpu$1
    local node_path=$(echo /sys/bus/cpu/devices/$cpu/node*)

    if [[ -d $node_path ]]; then
        return ${node_path#*/node}
    fi

    echo "ERROR: failed get NODE id for $cpu"
    exit -1
}

node_to_cpu() {
    local node=$1
    numactl -H | grep "node $node cpus:" | sed -e "s/.*: //g" -e "s/ /,/g"
}

hardware_detect() {
    local running_cpu=

    [[ -n $target_pid ]] || return 0

    # skip if user provided -h or -c
    [[ -z $hot_node ]] || return 0
    [[ -z $cold_node ]] || return 0

    running_cpu=$(ps -o psr $target_pid | tail -n +2)
    cpu_to_node $running_cpu
    hot_node=$?
    cold_node=$(($hot_node^1))

    # Consider to reduce dependency, we consider to rewrite below
    # rb by python.
    #│The more sophisticated solution is to use top node as COLD and 2nd
    #│large node as HOT, based on smaps stats:
    #│
    #│       % ./task-numa-maps.rb $$
    #│       N0  6M  99%
    #│       N4  0M  0%
    #│       N8  0M  0%
    #│
    #│But if user specified the values, just leave it to user.
    echo "PID $target_pid running on node $hot_node. HOT node:$hot_node COLD node:$cold_node.";
}

wait_pid_timeout()
{
    local wait_pid=$1
    local timeout=$2

    [[ -n $wait_pid ]] || return 0

    # no timeout ? so let's keep wattting.
    if [[ -z $timeout ]]; then
        wait $wait_pid
        return 0
    fi

    echo "Waitting for $wait_pid (Maximum $timeout seconds)"
    for i in $(seq 1 $timeout); do
        if [[ ! -d /proc/$wait_pid ]]; then
            break
        fi
        sleep 1
    done
    echo "Finished Wait for $wait_pid ($i/$timeout seconds)"
    return 0
}

kill_sys_refs()
{
    if [[ -z $sys_refs_pid ]]; then
        return 0
    fi

    if [[ -d /proc/$sys_refs_pid ]]; then
        echo "Killing sys_refs($sys_refs_pid)"
        kill $sys_refs_pid > /dev/null 2>&1
        sys_refs_pid=
    fi
}

kill_perf()
{
    if [[ ! -z $perf_pid ]]; then
        echo "killing perf($perf_pid)"
        kill $perf_pid > /dev/null 2>&1
        perf_pid=
    fi
}

run_perf_bw()
{
    $CALC_PERF_BW $(get_perf_path) $target_pid $perf_log $run_time
}

run_perf_ipc()
{
    local ipc_runtime=$1
    local log_file_suffix=$2

    local perf_log_ipc="$log_dir/perf-$target_pid-$dram_percent-ipc-$log_file_suffix.log"
    local event=(cycles instructions)
    local perf_cmd="$(get_perf_path) stat -p $target_pid -o $perf_log_ipc "
    local perf_cmd_end=" -- sleep $ipc_runtime"

    for i in ${event[@]}; do
        perf_cmd="$perf_cmd -e $(add_perf_event_modifier $i $workload_type) "
    done
    perf_cmd="$perf_cmd $perf_cmd_end"

    $perf_cmd > $perf_log_ipc 2>&1

    echo $($PARSER_PERF_IPC < $perf_log_ipc)
}

output_perf_ipc()
{
    echo ""
    echo "IPC baseline:        $perf_ipc_before instructions per cycle"
    echo "IPC after migration: $perf_ipc_after instructions per cycle"
    calc_ipc_drop $perf_ipc_before $perf_ipc_after
}

on_ctrlc()
{
    echo "Ctrl-C received"
    restore_pid_cpu_affinity
    kill_sys_refs
    kill_perf
    probe_kernel_module unload $DEFAULT_KERNEL_MODULE
    enable_numabalance_restore
}

calc_ipc_drop()
{
    local ipc_before=$1
    local ipc_after=$2
    $CALC_PERF_IPC $ipc_before $ipc_after
}

run_cold_page_bw_per_gb()
{
    echo "Gathering cold pages bandwidth per GB:"
    echo "log: $sys_refs_progressive_profile_log"
    cat /dev/null > $cold_page_bw_per_gb_log_list
    stdbuf -oL $SYS_REFS -d $dram_percent -c $SYS_REFS_YAML -p $CALC_COLD_PAGE_BW_PER_GB > $sys_refs_progressive_profile_log 2>&1
}

parse_cold_page_bw_per_gb()
{
    if  [[ ! -z $cold_page_bw_per_gb_log_list ]]; then
        $PARSER_COLD_PAGE_BW_PER_GB $log_dir $cold_page_bw_per_gb_log_list $dcpmem_bw_per_gb
    fi
}

save_pid_cpu_affinity()
{
    [[ -n $target_pid ]] || return

    target_pid_cpu_affinity=$(taskset -p $target_pid | cut -f2 -d: | sed -e "s/^ //g")
}

bind_pid_cpu_affinity()
{
    local target_node=$1
    local new_cpu_range=

    [[ -n $target_node ]] || return
    [[ -n $target_pid ]] || return

    new_cpu_range=$(node_to_cpu $target_node)
    if [[ -z $new_cpu_range ]]; then
        echo "Failed to bind target pid $target_pid to node $target_node"
        return
    fi

    taskset -a -pc $new_cpu_range $target_pid
}

restore_pid_cpu_affinity()
{
    [[ -n $target_pid ]] || return
    [[ -n $target_pid_cpu_affinity ]] || return

    taskset -a -p $target_pid_cpu_affinity $target_pid
}

create_log_dir()
{
    local log_dir=$1
    if [[ -d $log_dir ]]; then
        rm -rf "$log_dir.old"
        mv $log_dir $log_dir.old
    fi
    mkdir -p $log_dir
}

move_mem_to_node()
{
    local node=$1
    local pid=$2

    [[ -n $node ]] || return;
    [[ -n $pid ]] || return;

    migratepages $pid all $node
}


check_hw_compatibility()
{
    local family=$(hw_get_cpu_family_id)
    local model=$(hw_get_cpu_model_id)

    if [[ $family = "6" ]] && [[ $model = "85" ]]; then
        echo "Running on SLX/CLX platform [CPU_ID: family:$family model:$model]."
        return 0
    fi

    echo "WARNING: Running on unsupported platform [CPU_ID: family:$family model:$model], the result may not be accurate enough."
    return 0
}

calc_dcpmem_bw_per_gb()
{
    $CALC_DCPMEM_BW_PER_GB $(get_perf_path) \
                           $target_pid \
                           $log_dir \
                           $DCPMEM_HW_INFO_FILE \
                           $DCPMEM_BW_PER_GB_RUN_TIME \
                           $dcpmem_dimm_size \
                           $dcpmem_combine_type
}

find_kernel_module()
{
    local module_name=$1
    local find_result=

    if [[ -z $module_name ]]; then
        return 0
    fi

    find_result=$(lsmod | grep $module_name)
    if [[ -z $find_result ]]; then
        return 0
    fi
    return 1
}

probe_kernel_module()
{
    local action=$1
    local module_file=$2
    local ret=
    local output=
    local module_name=

    if [[ ! -f $module_file ]]; then
        echo "Can NOT find kernel module: $module_file"
        exit -1
    fi

    if [[ $action == "load" ]]; then

        # don't load same module but with different name/building again
        module_name=$(basename $module_file .ko)
        find_kernel_module $module_name
        if (( $? == 1 )); then
            return 0
        fi

        output=$(insmod $module_file)
        ret=$?
    elif [[ $action == "unload" ]]; then
        output=$(rmmod $module_file)
        ret=$?
    else
        output="Unknown action: $action"
        ret=1
    fi

    if  (( $ret != 0 )); then
        echo $output
        exit -1
    fi

    return 0
}

parse_hotness_drifting()
{
    echo ""
    echo "The hotness drifiting of pid $target_pid:"
    $PARSER_SYSREFS_HOTNESS_DRIFITING < $sys_refs_log
}

get_workload_type()
{
    local target_pid=$1
    local log="$log_dir/workload-type-$target_pid.log"
    local workload_type=

    workload_type=$($CALC_WORKLOAD_TYPE $(get_perf_path) $target_pid $log 2)
    echo "$workload_type"
}

disable_numabalance_save()
{
    [[ ! -f $NUMA_BALANCE_PATH ]] && return 0
    numa_balance_setting=$(cat $NUMA_BALANCE_PATH)
    echo 0 > $NUMA_BALANCE_PATH
}

enable_numabalance_restore()
{
    [[ -z $numa_balance_setting ]] && return 0
    echo $numa_balance_setting > $NUMA_BALANCE_PATH
    numa_balance_setting=
}

# START

trap 'on_ctrlc' INT
check_hw_compatibility
parse_parameter "$@"
hardware_detect
check_parameter

workload_type=$(get_workload_type $target_pid)
echo "Pid $target_pid type: $workload_type"

probe_kernel_module load $DEFAULT_KERNEL_MODULE

disable_numabalance_save
save_pid_cpu_affinity
bind_pid_cpu_affinity $hot_node

echo "Moving the memory of pid $target_pid into hot node $hot_node"
move_mem_to_node $hot_node $target_pid

echo "Gathering HW baseline data ($DCPMEM_BW_PER_GB_RUN_TIME seconds)"
dcpmem_bw_per_gb=$(calc_dcpmem_bw_per_gb)

echo "Gathering baseline IPC data ($PERF_IPC_RUN_TIME seconds)"
perf_ipc_before=$(run_perf_ipc $PERF_IPC_RUN_TIME "before")

prepare_sys_refs $target_pid

run_cold_page_bw_per_gb

echo "Moving the memory of pid $target_pid into cold node $cold_node"
move_mem_to_node $cold_node $target_pid

run_sys_refs
wait_pid_timeout $sys_refs_pid $SYS_REFS_RUNTIME
kill_sys_refs

run_perf_bw

echo "Gathering IPC data ($PERF_IPC_RUN_TIME seconds)"
perf_ipc_after=$(run_perf_ipc $PERF_IPC_RUN_TIME "after")

restore_pid_cpu_affinity
enable_numabalance_restore

parse_perf_log
parse_cold_page_bw_per_gb
parse_sys_refs_log
output_perf_ipc
parse_hotness_drifting

kill_perf
probe_kernel_module unload $DEFAULT_KERNEL_MODULE
