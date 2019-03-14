#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
# Authors: Yuan Yao <yuan.yao@intel.com>
#
# Test purpose:
#   For comparing 2LM and KUNPENG performance with sysbench global mode.
#
# Expected HW and system setup:
#
#   DRAM and AEP:
#     256GB AEP per physical NUMA socket at least
#     64GB DRAM per physical NUMA socket at least
#
#   NUMA toplogy:
#     Physical NUMA 0 -> 64GB DRAM
#     Physical NUMA 1 -> 64GB DRAM
#     Physical NUMA 2 -> 256GB AEP
#     Physical NUMA 3 -> 256GB AEP
#
#   These tests will run on NUMA 1 and 3.
#

cd "$(dirname "$0")"

../run-vm-tests.rb scheme-sysbench-memory-kunpeng-32t-global.yaml
sleep 10
../run-vm-tests.rb scheme-sysbench-memory-kunpeng-16t-global.yaml
sleep 10
../run-vm-tests.rb scheme-sysbench-memory-kunpeng-4t-global.yaml
sleep 10
../run-vm-tests.rb scheme-sysbench-memory-kunpeng-1t-global.yaml

