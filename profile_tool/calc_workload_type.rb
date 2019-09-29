#!/usr/bin/env ruby
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2019 Intel Corporation
#
#

WORKLOAD_TYPE_NORMAL = 1
WORKLOAD_TYPE_NORMAL_STR = "normal"

WORKLOAD_TYPE_KVM = 2
WORKLOAD_TYPE_KVM_STR = "kvm"

def get_workload_type(perf_path, target_pid, log_file, sample_time)

  guest_event_count = 0
  perf_cmd = "#{perf_path} stat -p #{target_pid} -e cycles:G -o #{log_file} -- sleep #{sample_time}"

  begin
    `#{perf_cmd}`
  rescue => e
    STDERR.puts e.message

    # assume normal workload in error case
    return WORKLOAD_TYPE_NORMAL
  end

  File.open(log_file, "r") do |file|
    file.each_line do |line|
      case line
      when /([\d,]+)\s+cycles/
        guest_event_count += $1.delete(",").to_f
      end
    end
  end

  return WORKLOAD_TYPE_KVM if guest_event_count != 0
  return WORKLOAD_TYPE_NORMAL
end

# For integrate with bash
if __FILE__ == $0
  perf_path = ARGV[0]
  target_pid = ARGV[1]
  log_file = ARGV[2]
  sample_time = ARGV[3]

  if (WORKLOAD_TYPE_KVM == get_workload_type(perf_path, target_pid, log_file, sample_time)) then
    puts WORKLOAD_TYPE_KVM_STR
  else
    puts WORKLOAD_TYPE_NORMAL_STR
  end
end
