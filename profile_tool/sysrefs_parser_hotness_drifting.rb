#!/usr/bin/env ruby

$stable = Hash.new {|hash, key| hash[key] = []}
$unstable = Hash.new {|hash, key| hash[key] = []}
$percent = Hash.new {|hash, key| hash[key] = []}

while (line = STDIN.gets)
  case line
  when /(\w+)\s+:\s+stable\s+pages:(\d+)\s+unstable\s+pages:(\d+)\s+drifting\s+per\s+minute:([\d\.]+)%/
    $stable[$1].push($2.to_i)
    $unstable[$1].push($3.to_i)
    $percent[$1].push($4.to_f)
  end
end

$percent.each_pair do |key, value_array|
  max = value_array.max
  min = value_array.min
  if value_array.size != 0
    avg = value_array.sum.to_f / value_array.size
  else
    avg = 0
  end

  print "%s: Max:%2.2f%%   Min:%2.2f%%   Avg:%2.2f%%\n" % [ key, max, min, avg ]
end
