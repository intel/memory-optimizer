#!/usr/bin/ruby
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#

require_relative "ProcNumaMaps"

proc_numa_maps = ProcNumaMaps.new
proc_numa_maps.load(ARGV[0])

proc_numa_maps.show_numa_placement
