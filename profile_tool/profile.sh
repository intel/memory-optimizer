#!/bin/bash


cd "$(dirname "$0")"

#config variable
sys_refs_yaml_template=sys-refs-template.yaml
sys_refs_yaml=sys-refs.yaml
sys_refs=sys-refs/sys-refs
perf=pmutools/ocperf.py

#parameter variable
target_pid=
dram_percent=25
hot_node=
cold_node=
run_time=1200

#runtime variable
sys_refs_pid=
perf_pid=
sys_refs_log=
perf_log=


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
                echo "-p $OPTARG"
                target_pid=$OPTARG
                ;;
            "d")
                echo "-d $OPTARG"
                dram_percent=$OPTARG
                ;;
            "h")
                echo "-h $OPTARG"
                hot_node=$OPTARG
                ;;
            "c")
                echo "-c $OPTARG"
                cold_node=$OPTARG
                ;;
            "t")
                echo "-t $OPTARG"
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

    echo "target PID = $target_pid"
    echo "DRAM percent = $dram_percent"
    echo "time = $run_time"
    echo "hot node = $hot_node"
    echo "cold node = $cold_node"

    sys_refs_log=sys-refs-$target_pid-$dram_percent.log
    perf_log=perf-$target_pid-$dram_percent.log
}

prepare_sys_refs() {
    local pid=$1
    if  [[ ! -e $sys_refs_yaml_template ]]; then
        echo "Error: No $sys_refs_yaml_template file"
        exit 10
    fi

    echo "prepare the sys-refs configuration file"
    cat $sys_refs_yaml_template > $sys_refs_yaml

    # NUMA part
    cat >> $sys_refs_yaml <<EOF
  numa_nodes:
    $hot_node:
      type: DRAM
      demote_to: $cold_node
    $cold_node:
      type: PMEM
      demote_to: $hot_node
EOF
    # policy part
    cat >> $sys_refs_yaml <<EOF

policies:
  - pid: $pid
    dump_distribution: true

EOF
}

run_sys_refs() {
    echo "log: $sys_refs_log"
    stdbuf -oL $sys_refs -d $dram_percent -c $sys_refs_yaml > $sys_refs_log 2>&1 &
    sys_refs_pid=$!
    echo "sys-refs pid: $sys_refs_pid"
}

run_perf() {
    echo "Empty run_perf"
    # Step1 perf list | grep to find the event name
    # Step2 construct the cmd line parameter by pid and event name
    # Step3 run the perf > $perf_log
    #
    #

    #$perf list
    #perf_pid=$!
}

parse_perf_log() {
    # TODO: parse the log from $perf_log
    echo "Empty parse_perf_log"
}

kill_child() {
    local pid
    for pid; do
        echo "killing $1"
        kill $pid
    done
}

cpu_to_node() {
    local cpu=cpu$1
    
    # 64 NODEs should be enough in non fake NUMA state
    for i in {0..63}; do
        echo $i
        if [[ -d /sys/bus/cpu/devices/$cpu/node$i ]]; then
            return $i
        fi        
    done
    echo "ERROR: failed to get NODE id for $cpu"
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
    echo "hardware detection: hot node:$hot_node cold node:$cold_node";
}

parse_parameter "$@"
hardware_detect
check_parameter

prepare_sys_refs $target_pid
run_sys_refs
run_perf

#let sys-refs and perf run
sleep $run_time

parse_perf_log

kill_child $sys_refs_pid
kill_child $perf_pid
