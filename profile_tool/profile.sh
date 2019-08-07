#!/bin/bash

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/variable_setup.sh"

SYS_REFS_RUNTIME=600

COLD_BW_PER_GB_SCRIPT=$BASE_DIR/cold_page_bw_per_gb.sh

# parameter variable
target_pid=
dram_percent=25
hot_node=
cold_node=
run_time=1200

# runtime variable
sys_refs_pid=
perf_pid=
sys_refs_log=
sys_refs_progressive_profile_log=
perf_log=
cold_page_bw_per_gb_log_list=

# parser
PERF_IPC_PARSER=$BASE_DIR/perf_parser_ipc.rb
PERF_IPC_CALC=$BASE_DIR/perf_calc_ipc.rb
PERF_BW_PARSER=$BASE_DIR/perf_parser_bw.rb
SYSREFS_RATIO_PARSER=$BASE_DIR/sysrefs_parser_ratio.rb
COLD_PAGE_BW_PER_GB_PARSER=$BASE_DIR/bw_per_gb_parser.rb

usage() {
    echo "usage: $0:"
    echo "       -p target PID."
    echo "       -d dram percent for target PID."
    echo "       -h NUMA node id for HOT pages."
    echo "       -c NUMA node id for COLD pages."
    echo "       -t Run time, in second unit."
    echo "For example:"
    echo "the target pid is 100, we want to leave 25% hot pages to NUMA node 1,"
    echo "leave remain cold pages to NUMA node 3, run total 20 minutes:"
    echo "$0 -p 100 -d 25 -h 1 -c 3 -t 1200"
}

parse_parameter() {
    while getopts ":p:d:h:c:t:" optname; do
        case "$optname" in
            "p")
                # echo "-p $OPTARG"
                target_pid=$OPTARG
                ;;
            "d")
                # echo "-d $OPTARG"
                dram_percent=$OPTARG
                ;;
            "h")
                # echo "-h $OPTARG"
                hot_node=$OPTARG
                ;;
            "c")
                # echo "-c $OPTARG"
                cold_node=$OPTARG
                ;;
            "t")
                # echo "-t $OPTARG"
                run_time=$OPTARG
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
        echo "Please use -h to indicate NUMA node for HOT pages"
        will_exit=1
    fi

    if [[ -z $cold_node ]]; then
        echo "Please use -c to indicate NUMA node for COLD pages"
        will_exit=1
    fi

    if [[ -z $target_pid ]]; then
        echo "Please use -p to indicate target PID"
        will_exit=1
    fi

    if [[ $will_exit -eq 1 ]]; then
        usage
        exit 1
    fi

    echo "parameter dump:"
    echo "  target PID = $target_pid"
    echo "  DRAM percent = $dram_percent"
    echo "  time = $run_time"
    echo "  hot node = $hot_node"
    echo "  cold node = $cold_node"

    sys_refs_log=sys-refs-$target_pid-$dram_percent.log
    perf_log=perf-$target_pid-$dram_percent.log
    sys_refs_progressive_profile_log=sys-refs-progress-profile-$target_pid.log
    cold_page_bw_per_gb_log_list=$COLD_PAGE_BW_PER_GB_LOG_LIST-$target_pid.log
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

    if [[ ! -f $sys_refs_log ]]; then
        return 0;
    fi

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

hardware_detect() {
    local running_cpu=

    # skip if user provided -h or -c
    if [[ ! -z $hot_node ]]; then
        return 0
    fi
    if [[ ! -z $cold_node ]]; then
        return 0
    fi

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

    if [[ -z $wait_pid ]]; then
        return 0
    fi

    # no timeout ? so let's keep wattting.
    if [[ -z $timeout ]]; then
        wait $wait_pid
        return 0
    fi

    for i in $(seq 1 $timeout); do
        if [[ ! -d /proc/$wait_pid ]]; then
            break
        fi
        sleep 1
        printf "\rWaitting for $wait_pid ($i/$timeout seconds)"
    done
    printf "\n"
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
    stdbuf -oL $SYS_REFS -d $dram_percent -c $SYS_REFS_YAML -p $COLD_BW_PER_GB_SCRIPT 2>&1 | tee $sys_refs_progressive_profile_log
}

parse_cold_page_bw_per_gb()
{
    if  [[ ! -z $cold_page_bw_per_gb_log_list ]]; then
        $COLD_PAGE_BW_PER_GB_PARSER $cold_page_bw_per_gb_log_list
    fi
}


trap 'on_ctrlc' INT

parse_parameter "$@"
hardware_detect
check_parameter

echo "Gathering basline IPC data (60 seconds)"
perf_ipc_before=$(run_perf_ipc 60)

prepare_sys_refs $target_pid

run_cold_page_bw_per_gb
parse_cold_page_bw_per_gb

run_sys_refs
wait_pid_timeout $sys_refs_pid $SYS_REFS_RUNTIME
kill_sys_refs

run_perf_bw

echo "Gathering IPC data (60 seconds)"
perf_ipc_after=$(run_perf_ipc 60)

parse_perf_log
parse_sys_refs_log
output_perf_ipc

kill_perf
