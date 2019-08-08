#!/bin/ruby

$bw_per_gb_by_page_type = {}

LLC_CACHE_LINE_SIZE = 64
def byte_to_MBs(hit_count, time)
  hit_count * LLC_CACHE_LINE_SIZE / (1000000.0 * time)
end

def byte_to_KB(byte)
  byte / 1024
end

def format_number(number)
  str = number.to_s
  next_str = nil
  while (true)
    next_str = str.gsub(/(\d+)(\d\d\d)/, '\1,\2')
    break if 0 == (str <=> next_str)
    str = next_str
  end
  str
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
  total_byte = page_count * page_size
  bw_per_gb = 1000000000 * remote_read_cold / total_byte

  {:state => state, :ref_count => ref_count,
    :page_size => page_size, :bw_per_gb => bw_per_gb,
    :total_byte => total_byte}

end

def save_result(hash_table, result)
  key = result[:page_size]
  ref_index = result[:ref_count]
  hash_table[key] = [] unless hash_table.has_key? key
  hash_table[key][ref_index] = result
end

def output_bw_per_gb(bw_per_gb_result)
  failed_count = 0

  bw_per_gb_all = 0
  total_all = 0
  count_all = 0

  bw_per_gb_result.each_pair do |page_type, result|
    print "\n%dK-page histogram:\n" % [byte_to_KB(page_type)]
    puts "ref_count    MBps-per-GB         size(KB)\n"
    puts "==========================================\n"

    bw_per_gb_page_type = 0
    total_page_type = 0
    count_page_type = 0

    result.reverse.each do |one_result|
      next if one_result == nil
      if one_result[:state] == :success
        bw_per_gb_page_type += one_result[:bw_per_gb]
        total_page_type += one_result[:total_byte]
        count_page_type += 1

        print "%9d %14.2f %16s\n" \
              % [ one_result[:ref_count], one_result[:bw_per_gb], \
                  format_number(byte_to_KB(one_result[:total_byte])) ]
      else
        failed_count = failed_count + 1
      end
    end

    next if 0 == count_page_type
    bw_per_gb_all += bw_per_gb_page_type
    count_all += count_page_type
    total_all += total_page_type

    bw_per_gb_page_type /= count_page_type
    total_page_type = byte_to_KB(total_page_type)
    print "%dK-page average BW-per-GB: %.2f\n" \
          % [ byte_to_KB(page_type), bw_per_gb_page_type ]
    print "%dK-page total size:        %s KB\n" \
          % [ byte_to_KB(page_type), format_number(total_page_type) ]
  end

  bw_per_gb_all /= count_all
  total_all = byte_to_KB(total_all)
  print "\nAll average BW-per-GB: %.2f\n" % [ bw_per_gb_all ]
  print "All total size:        %s KB\n" % [ format_number(total_all) ]
  puts "failed count: #{failed_count}" if failed_count > 0

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

output_bw_per_gb($bw_per_gb_by_page_type)
