#!/bin/sh
# - nr_task
# - runtime
# - build_kconfig
# - target

## Build linux kernel
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Peng Bo <bo2.peng@intel.com>
#

: ${nr_task:=8}
: ${runtime:=300}
: ${build_kconfig:=defconfig}
: ${BENCHMARK_ROOT:=/root/linux}
: ${dir_name:=/root/pengbo-workspace/kernel-build}
: ${script_name:=script}

[ -n "$target" ] || target=vmlinux
log_echo()
{
  date=$(date +'%F %T')
  echo "$date $@"
}

log_eval()
{
  log_echo "$@"
  eval "$@"
}

log_cmd()
{
  log_echo "$@"
  "$@"
}

setup_sys()
{
  echo $thp > /sys/kernel/mm/transparent_hugepage/enabled
  echo 0 > /proc/sys/kernel/numa_balancing
}

numa_cmd()
{
	local node=$1
	numactl -m $node
}


run_kbuild()
{
	local node=$1
	start_time=$(date +%s)
	iterations=0
	while true; do
		echo 3 > /proc/sys/vm/drop_caches
		numactl -m $node make mrproper | tee -a ${log_file}
		numactl -m $node make $build_kconfig | tee -a ${log_file}
		numactl -m $node make -j $nr_task $target 2> /dev/null | tee -a ${log_file}
		iterations=$((iterations + 1))
		now=$(date +%s)
		[ $((now - start_time)) -gt "$runtime" ] && break
	done
	echo "iterations: $iterations"
	echo "runtime: $((now - start_time))"
}

cd $BENCHMARK_ROOT || {
	echo "ERROR: no kernel source code in $BENCHMARK_ROOT" 1>&2
	exit 1
}

thp=always
setup_sys
run_kbuild 1
