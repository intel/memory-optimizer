#!/bin/ruby

dram_kb=0
dram_ratio=0
pmem_kb=0
pmem_ratio=0

while (line = STDIN.gets)
  case line
  when /Anon DRAM.*:\s*([\d,]+)\s*([\d%]+)/
    dram_kb = $1.delete(",").to_i
    dram_ratio = $2.to_s
  when /Anon PMEM.*:\s*([\d,]+)\s*([\d%]+)/
    pmem_kb = $1.delete(",").to_i
    pmem_ratio = $2.to_s
  end
end

dram_kb = dram_kb / 1024.0
pmem_kb = pmem_kb / 1024.0
puts "HOT page distribution:  #{dram_kb} MB #{dram_ratio}"
puts "COLD page distribution: #{pmem_kb} MB #{pmem_ratio}"
