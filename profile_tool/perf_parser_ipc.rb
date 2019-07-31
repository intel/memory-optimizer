#!/bin/ruby

# parser IPC from perf output
while (line = STDIN.gets)
  case line
  when /#\s*(\d*.*insn per cycle\s*$)/
    puts $1
  end
end
