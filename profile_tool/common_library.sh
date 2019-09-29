#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
#

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/variable_setup.sh"

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

get_perf_path()
{
    if [[ -f $PMUTOOLS_PERF ]]; then
        echo $PMUTOOLS_PERF
    else
        echo $PERF
    fi

    return 0
}

add_perf_event_modifier()
{
    local event=$1
    local type=$2
    local modifier=
    local last_char=${event:0-1:1}

    if [[ $last_char != "/" ]]; then
        modifier=":"
    else
        modifier=""
    fi

    if [[ $type == "kvm" ]]; then
        modifier=$modifier"G"
    else
        modifier=$modifier"u"
    fi

    echo "$event$modifier"
}
