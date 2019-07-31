#!/bin/ruby

total_read=0
remote_read_cold=0
local_read_hot=0

remote_read_cold_percent=0
local_read_hot_percent=0

time=1

while (line = STDIN.gets) do
  case line
  when /([\d,]*)\s*total_read\s*/
    total_read=$1.delete(",").to_i
  when /([\d,]*)\s*remote_read_COLD\s*/
    remote_read_cold=$1.delete(",").to_i
  when /([\d,]*)\s*local_read_HOT\s*/
    local_read_hot=$1.delete(",").to_i
  when /([\d\.]*)\s*seconds\s*time\s*elapsed\s*$/
    time=$1.to_f
  end
end

# hit count * 64 = bytes, then convert to MB/s
total_read = total_read * 64 / (1000000.0 * time)
remote_read_cold = remote_read_cold * 64 / (1000000.0 * time)
local_read_hot = local_read_hot * 64 / (1000000.0 * time)

remote_read_cold_percent = 100.0 * (remote_read_cold / total_read)
local_read_hot_percent = 100.0 * (local_read_hot / total_read)

puts "Bandwidth: Total_read:       %.3f MB/s" % total_read
puts "Bandwidth: Local_read_HOT:   %.3f MB/s %.3f%%" % [local_read_hot, local_read_hot_percent]
puts "Bandwidth: Remote_read_COLD: %.3f MB/s %.3f%%" % [remote_read_cold, remote_read_cold_percent]
