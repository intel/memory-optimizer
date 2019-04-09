#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#          Yuan Yao <yuan.yao@intel.com>
#

[[ $interleave ]] && numactl="numactl --interleave=$interleave --"

sysbench_cmd=(
	$numactl
	stdbuf -oL
	sysbench
	--time=$time
	memory
	--memory-block-size=$memory_block_size
	--memory-total-size=1024T
	--memory-scope=$memory_scope
	--memory-oper=$memory_oper
	--memory-access-mode=rnd
	--rand-type=$rand_type
    --rand-gaussian-stddev=$gaussian_stddev
	--rand-pareto-h=0.1
	--threads=$threads
	run
)

echo "${sysbench_cmd[@]}"
time "${sysbench_cmd[@]}" &

[[ $pid_file ]] && echo $! > $pid_file
