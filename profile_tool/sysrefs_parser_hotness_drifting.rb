#!/bin/ruby

$page = Hash.new {|hash, key| hash[key] = []}

while (line = STDIN.gets)
  case line
  when /(\w+)\s+:\s+stable\s+pages:(\d+)\s+unstable\s+pages:(\d+)\s+drifting\s+percent:(\d+)%/
    $page[$1].push( {:stable => $2.to_i, :unstable => $3.to_i, :percent => $4.to_i} )
  end
end

def calculate_hotness_drifting(result_array)
  return 0, 0, 0 if result_array.empty?

  max = result_array.max { |a, b| a[:percent] <=> b[:percent] }
  min = result_array.min { |a, b| a[:percent] <=> b[:percent] }
  total = result_array.sum { |a| a[:percent] }

  return max[:percent], min[:percent], total.to_f / result_array.size
end

$page.each_pair do |key, value|
  max, min, avg = calculate_hotness_drifting(value)
  print "%s: Max:%2d%%   Min:%2d%%   Avg:%2.2f%%\n" % [ key, max, min, avg ]
end
