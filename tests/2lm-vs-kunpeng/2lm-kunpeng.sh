#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
# Authors: Yuan Yao <yuan.yao@intel.com>
#


cd "$(dirname "$0")"

./run-vm-tests.rb scheme-sysbench-memory-kunpeng-32t.yaml
sleep 10
./run-vm-tests.rb scheme-sysbench-memory-kunpeng-16t.yaml
sleep 10
./run-vm-tests.rb scheme-sysbench-memory-kunpeng-4t.yaml
sleep 10
./run-vm-tests.rb scheme-sysbench-memory-kunpeng-1t.yaml

