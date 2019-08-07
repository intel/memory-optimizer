#!/bin/ruby

$bw_per_gb_by_page_type = {}
$failed_count = 0
# hit count * 64 = bytes, then convert to MB/s
def byte_to_MBs(hit_count, time)
  hit_count * 64 / (1000000.0 * time)
end

def parse_one_file(file_name)

  remote_read_cold = 0

  ref_count = 0
  page_size = 0
  page_count = 0

  bw_per_gb = 0

  time = 1
  state = :success;
  begin
    file = File.new(file_name, "r")
    file.each do |line|
      case line
      when /([\d,]*)\s+remote_read_COLD/
        remote_read_cold = $1.delete(",").to_i
      when /([\d\.]*)\s+seconds\s+time\s+elapsed/
        time = $1.to_f
      when /.*ref_count=(\d+)/
        ref_count = $1.to_i
      when /.*page_size=(\d+)/
        page_size = $1.to_i
      when /.*page_count=(\d+)/
        page_count = $1.to_i
      end
    end
  rescue StandardError => e
    puts "Warning parse one file: #{e.message}"
    state = :fail
  end

  remote_read_cold = byte_to_MBs(remote_read_cold, time)
  bw_per_gb = remote_read_cold / (page_count * page_size)
  {:state => state, :ref_count => ref_count, :page_size => page_size, :bw_per_gb => bw_per_gb}
end

def save_result(hash_table, result)
  key = result[:page_size]
  ref_index = result[:ref_count]
  hash_table[key] = [] unless hash_table.has_key? key
  hash_table[key][ref_index] = result
end

# Start here
begin
  file_name = ARGV[0]
  list_file = File.new(file_name, "r")
  list_file.each do |line|
    result = parse_one_file(line.chomp)
    save_result($bw_per_gb_by_page_type, result)
  end
rescue StandardError => e
  puts "Warning: #{e.message}"
end

puts "Cold pages BW per GB:"
$bw_per_gb_by_page_type.each_pair do |page_type, result|
  puts "  Page size: #{page_type}:"
  result.each do |one_result|
    if one_result[:state] == :success
      puts "    ref_count[#{one_result[:ref_count]}]: #{one_result[:bw_per_gb]}"
    else
      $failed_count = $failed_count + 1
    end
  end
end
puts "    failed count: #{$failed_count}"
