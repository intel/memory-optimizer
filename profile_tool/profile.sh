#!/usr/bin/env bash

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/variable_setup.sh"

# parameter variable
target_pid=
dram_percent=25
hot_node=
cold_node=
run_time=1200
dcpmem_size_mb=
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
start_timestamp=
dcpmem_bw_per_gb=

# sub scripts
PERF_IPC_PARSER=$BASE_DIR/perf_parser_ipc.rb
PERF_IPC_CALC=$BASE_DIR/perf_calc_ipc.rb

PERF_BW_PARSER=$BASE_DIR/perf_parser_bw.rb

SYSREFS_RATIO_PARSER=$BASE_DIR/sysrefs_parser_ratio.rb

COLD_BW_PER_GB_SCRIPT=$BASE_DIR/cold_page_bw_per_gb.sh
COLD_PAGE_BW_PER_GB_PARSER=$BASE_DIR/bw_per_gb_parser.rb

DCPMEM_BW_PER_GB_CALC=$BASE_DIR/dcpmem_hw_bw_per_gb.rb

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
       -s DCPMEM total size in system, in MB unit.
       -m DCPMEM DIMM type: 128 256 512.
       -i DCPMEM configuration: single 211 221 222.

For example:
The system has total 512G DCPMEM, combined by 128 X 4 in 2-1-1 configuration,
the target pid is 100, we want to leave 25% hot pages to NUMA node 1,
leave remain cold pages to NUMA node 3, run total 20 minutes:
$0 -p 100 -d 25 -h 1 -c 3 -t 1200 -s 524288 -m 128 -i 211

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
            "s")
                dcpmem_size_mb=$OPTARG
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
    local log_dir=
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

    if [[ -z $dcpmem_size_mb ]]; then
        echo "Please use -s to indicate the total DPCMEM size in system."
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
  DCPMEM total size: $dcpmem_size_mb MB
  DCPMEM dimm size: $dcpmem_dimm_size
  DCPMEM configuration: $dcpmem_combine_type
EOF

    log_dir=$(get_log_dir $target_pid)
    sys_refs_log=$log_dir/sys-refs-$target_pid-$dram_percent.log
    perf_log=$log_dir/perf-$target_pid-$dram_percent.log
    sys_refs_progressive_profile_log=$log_dir/sys-refs-progress-profile-$target_pid.log
    cold_page_bw_per_gb_log_list=$log_dir/$COLD_PAGE_BW_PER_GB_LOG_LIST-$target_pid.log

    if [[ -d $log_dir ]]; then
        rm $log_dir/*
    else
        mkdir -p $log_dir
    fi
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
    $PERF_BW_PARSER < $perf_log
}

parse_sys_refs_log() {

    [[ -f $sys_refs_log ]] || return 0;


    echo ""
    $SYSREFS_RATIO_PARSER < $sys_refs_log
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
    echo "\n"
    return 0
}

kill_sys_refs()
{
    if [[ ! -z $sys_refs_pid ]]; then
        echo "killing sys_refs($sys_refs_pid)"
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
    $PERF_BW_SCRIPT $PERF $target_pid $perf_log $run_time
}

run_perf_ipc()
{
    local ipc_runtime=$1
    local perf_log_ipc=perf-$target_pid-$dram_percent-ipc.log
    local perf_cmd="$PERF stat -p $target_pid \
                    -e cycles,instructions \
                    -o $perf_log_ipc -- sleep $ipc_runtime"

    $perf_cmd > $perf_log_ipc 2>&1

    echo $($PERF_IPC_PARSER < $perf_log_ipc)
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
}

calc_ipc_drop()
{
    local ipc_before=$1
    local ipc_after=$2
    $PERF_IPC_CALC $ipc_before $ipc_after
}

run_cold_page_bw_per_gb()
{
    echo "Gathering cold pages bandwidth per GB:"
    echo "log: $sys_refs_progressive_profile_log"
    cat /dev/null > $cold_page_bw_per_gb_log_list
    stdbuf -oL $SYS_REFS -d $dram_percent -c $SYS_REFS_YAML -p $COLD_BW_PER_GB_SCRIPT > $sys_refs_progressive_profile_log 2>&1
}

parse_cold_page_bw_per_gb()
{
    if  [[ ! -z $cold_page_bw_per_gb_log_list ]]; then
        $COLD_PAGE_BW_PER_GB_PARSER $(get_log_dir $target_pid) $cold_page_bw_per_gb_log_list
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
    local pid=$2
    local new_cpu_range=

    [[ -n $target_node ]] || return
    [[ -n $pid ]] || return

    new_cpu_range=$(node_to_cpu $target_node)
    if [[ -z $new_cpu_range ]]; then
        echo "Failed to bind target pid $pid to node $target_node"
        return
    fi

    taskset -pc $new_cpu_range $pid
}

restore_pid_cpu_affinity()
{
    [[ -n $target_pid ]] || return
    [[ -n $target_pid_cpu_affinity ]] || return

    taskset -p $target_pid_cpu_affinity $target_pid
}

mv_log_dir()
{
    local log_dir=$(get_log_dir $target_pid)
    if [[ -d $log_dir ]]; then
        mv $log_dir $log_dir_$start_timestamp
    fi
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

    if [[ $family = "6" ]] && [[ $model = 85 ]]; then
        echo "CPU_ID: family:$family model:$model Running on SLX platform."
        return 0
    fi

    echo "CPU_ID: family:$family model:$model Running on unsupported platform."
    exit -1
}

calc_dcpmem_bw_per_gb()
{
    $DCPMEM_BW_PER_GB_CALC $PERF \
        $target_pid \
        $DCPMEM_BW_PER_GB_RUN_TIME \
        $dcpmem_size_mb \
        $dcpmem_dimm_size \
        $dcpmem_combine_type
}

parse_dcpmem_bw_per_gb()
{
    echo "HW BW-per-GB: $dcpmem_bw_per_gb"
}

trap 'on_ctrlc' INT

start_timestamp=$(date +"%F-%H-%M-%S")

# Enable this before we release
# check_hw_compatibility
parse_parameter "$@"
hardware_detect
check_parameter

save_pid_cpu_affinity
bind_pid_cpu_affinity $hot_node $target_pid
move_mem_to_node $hot_node $target_pid

echo "Gathering HW baseline data ($DCPMEM_BW_PER_GB_RUN_TIME seconds)"
dcpmem_bw_per_gb=$(calc_dcpmem_bw_per_gb)

echo "Gathering baseline IPC data ($PERF_IPC_RUN_TIME seconds)"
perf_ipc_before=$(run_perf_ipc $PERF_IPC_RUN_TIME)

prepare_sys_refs $target_pid

run_cold_page_bw_per_gb

run_sys_refs
wait_pid_timeout $sys_refs_pid $SYS_REFS_RUNTIME
kill_sys_refs

run_perf_bw

echo "Gathering IPC data ($PERF_IPC_RUN_TIME seconds)"
perf_ipc_after=$(run_perf_ipc $PERF_IPC_RUN_TIME)

restore_pid_cpu_affinity

parse_perf_log
parse_cold_page_bw_per_gb
parse_dcpmem_bw_per_gb
parse_sys_refs_log
output_perf_ipc

kill_perf
