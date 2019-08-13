#!/bin/ruby

page_size_4k = []
page_size_2m = []

while (line = STDIN.gets)
  case line
  when /4K_accessed\s+:\s+stable\s+pages:(\d+)\s+unstable\s+pages:(\d+)\s+drifting\s+percent:(\d+)%/
    page_size_4k.push( {:stable => $1.to_i, :unstable => $2.to_i, :percent => $3.to_i} )
  when /2M_accessed\s+:\s+stable\s+pages:(\d+)\s+unstable\s+pages:(\d+)\s+drifting\s+percent:(\d+)%/
    page_size_2m.push( {:stable => $1.to_i, :unstable => $2.to_i, :percent => $3.to_i} )
  end
end

def calculate_hotness_drafting(result_array)
  return 0, 0, 0 if result_array.empty?

  max_percent = 0
  min_percent = 100
  total_percent = 0

  result_array.each do |value|
    max_percent = value[:percent] if value[:percent] >= max_percent
    min_percent = value[:percent] if value[:percent] <= min_percent
    total_percent += value[:percent]
  end

  return max_percent, min_percent, total_percent.to_f / result_array.size
end


puts "Hotness drifting:"
[
  {:page_type => "4k page size", :result => page_size_4k},
  {:page_type => "2M page size", :result => page_size_2m},
].each do |each|
  max, min, avg = calculate_hotness_drafting(each[:result])
  print "%s: Max:%2d%%   Min:%2d%%   Avg:%2.2f%%\n" % [ each[:page_type], max, min, avg]
end
