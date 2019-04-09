#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
# Authors: Yuan Yao <yuan.yao@intel.com>
#

cd "$(dirname "$0")"

#../run-vm-tests.rb scheme-sysbench-memory-2lm-1t.yaml
#sleep 10
#../run-vm-tests.rb scheme-sysbench-memory-2lm-4t.yaml
#sleep 10
#../run-vm-tests.rb scheme-sysbench-memory-2lm-16t.yaml
#sleep 10
../run-vm-tests.rb scheme-sysbench-memory-2lm-32t.yaml
#sleep 10
#../run-vm-tests.rb scheme-sysbench-memory-2lm-16t.yaml
