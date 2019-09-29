#!/usr/bin/env ruby
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
#

instructions = 0
cycles =1
ipc = 0

# parser IPC from perf output
while (line = STDIN.gets)
  case line
  # when /#\s*(\d*.*insn per cycle\s*$)/
  #   puts $1
  when /([\d,]*)\s+cycles/
    cycles = $1.delete(",").to_i
  when /([\d,]*)\s+instructions/
    instructions = $1.delete(",").to_i
  end
end

ipc = instructions.to_f / cycles
puts "%.4f" % ipc
