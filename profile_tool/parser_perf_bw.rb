#!/usr/bin/env ruby
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
#

total_read = 0
remote_read_cold = 0
local_read_hot = 0

remote_read_cold_percent = 0
local_read_hot_percent = 0

time = 1

while (line = STDIN.gets) do
  case line
  when /([\d,]*)\s+total_read/
    total_read = $1.delete(",").to_i
  when /([\d,]*)\s+remote_read_COLD/
    remote_read_cold = $1.delete(",").to_i
  when /([\d,]*)\s+local_read_HOT/
    local_read_hot = $1.delete(",").to_i
  when /([\d\.]*)\s+seconds\s+time\s+elapsed/
    time = $1.to_f
  end
end

def percentage(a,b)
  return 100.0 * a / b
end

def byte_to_MBs(byte, time)
  byte * 64 / (1000000.0 * time)
end

# hit count * 64 = bytes, then convert to MB/s
total_read = byte_to_MBs(total_read, time)
remote_read_cold = byte_to_MBs(remote_read_cold, time)
local_read_hot = byte_to_MBs(local_read_hot, time)

remote_read_cold_percent = percentage(remote_read_cold, total_read)
local_read_hot_percent = percentage(local_read_hot, total_read)

puts "Bandwidth: Total_read:       %.3f MB/s" % total_read
puts "Bandwidth: Local_read_HOT:   %.3f MB/s %.3f%%" % [local_read_hot, local_read_hot_percent]
puts "Bandwidth: Remote_read_COLD: %.3f MB/s %.3f%%" % [remote_read_cold, remote_read_cold_percent]
