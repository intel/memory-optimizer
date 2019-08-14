#!/usr/bin/env ruby


if ARGV.size < 2
  puts "No enough parameter to calculate the IPC Drop"
  return 0
end

ipc = ARGV[0].to_f
ipc_migrated = ARGV[1].to_f
drop = 100.0 * ((ipc_migrated / ipc) - 1.0)
puts "IPC Drop: %+.2f%%" % drop
