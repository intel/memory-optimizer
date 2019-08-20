#!/usr/bin/env ruby
require "yaml"
require_relative "dcpmem_hw_bandwidth_variable.rb"

perf            = ARGV[0]
target_pid      = ARGV[1]
perf_runtime    = ARGV[2] || 30
dcpmem_size_mb  = ARGV[3] || 0
dimm_size       = ARGV[4] || "256"
power_budget    = ARGV[5] || "15"
combine_type    = ARGV[6] || "222"

HW_INFO_FILE = "dcpmem-hw-info.yaml"
FALLBACK_SEQUENCE_INDICATOR = 50

perf_log = "dcpmem-bw-per-gb-pid-#{target_pid}.log"
dcpmem_hw_info = nil
sequence_indicator = 0
hw_seq_bandwidth = 0
hw_rand_bandwidth = 0
hw_bw_per_gb = 0

def run_perf(perf_path, run_time, target_pid, log_file)
  perf_begin = [ "#{perf_path}", "stat",
                 "-p #{target_pid}" ]
  perf_event = [ "-e offcore_response.all_pf_data_rd.any_response",
                 "-e offcore_response.all_data_rd.any_response",
                 "-e l2_rqsts.all_pf",
                 "-e l2_rqsts.all_demand_data_rd" ]
  perf_end = [ "-o #{log_file}",
               "-- sleep #{run_time}" ]

  perf_cmd = perf_begin + perf_event + perf_end
  perf_cmd = perf_cmd.join(" ")

  begin
    `#{perf_cmd}`
  rescue => e
    # puts e.message
    return false
  end
  return true
end

def calc_sequence_indicator(perf_log_file)
  perf_all_data_rd_pf = nil
  perf_all_data_rd = nil
  perf_l2_rqsts_all_pf = nil
  perf_l2_rqsts_all_demand = nil

  File.open(perf_log_file, "r") do |file|
    file.each_line do |line|
      case line
      when /([\d,]+)\s+offcore_response_all_pf_data_rd_any_response/
        perf_all_data_rd_pf = $1.delete(",").to_f
      when /([\d,]+)\s+offcore_response_all_data_rd_any_response/
        perf_all_data_rd = $1.delete(",").to_f
      when /([\d,]+)\s+l2_rqsts_all_pf/
        perf_l2_rqsts_all_pf = $1.delete(",").to_f
      when /([\d,]+)\s+l2_rqsts_all_demand_data_rd/
        perf_l2_rqsts_all_demand = $1.delete(",").to_f
      end
    end
  end

  if (perf_all_data_rd && perf_all_data_rd_pf)
    return perf_all_data_rd_pf / (perf_all_data_rd + 1.0)
  end

  if (perf_l2_rqsts_all_pf && perf_l2_rqsts_all_demand)
    return perf_l2_rqsts_all_pf / (perf_l2_rqsts_all_demand + perf_l2_rqsts_all_pf + 1.0)
  end

  # no valid perf data, so return 50% here
  return FALLBACK_SEQUENCE_INDICATOR
end

def get_dcpmem_hw_info(hw_info_hash_table,
                       dimm_size, power_budget, combine_type,
                       access_type, read_write)
  key_array = [ dimm_size.to_s, power_budget.to_s,
                combine_type.to_s, access_type.to_s, read_write.to_s ]

  hash_obj = hw_info_hash_table
  key_array.each do |each_key|
    hash_obj = hash_obj[each_key]
    if not hash_obj
      # puts "Failed to get dcpmem hw info with field \"#{each_key}\""
      return 0.0
    end
  end

  # the hash_obj is value here
  return hash_obj.to_f
end

def calc_hw_bw_per_gb(sequence_indicator,
                      hw_seq_bandwidth, hw_rand_bandwidth,
                      dcpmem_size_mb)
  hw_bandwidth = 0
  return 0 if dcpmem_size_mb == 0
  return 0 if hw_seq_bandwidth == 0
  return 0 if hw_rand_bandwidth == 0

  hw_bandwidth = hw_rand_bandwidth + sequence_indicator * (hw_seq_bandwidth - hw_rand_bandwidth)
  hw_bandwidth = 0 if hw_bandwidth < 0
  return 1000.0 * (hw_bandwidth / dcpmem_size_mb)
end

# START
begin
  dcpmem_hw_info = YAML.load_file(HW_INFO_FILE)
rescue => e
  # puts e.message
  puts "0"
  exit -1
end

if run_perf(perf, perf_runtime, target_pid, perf_log)
  sequence_indicator = calc_sequence_indicator(perf_log)
  hw_seq_bandwidth = get_dcpmem_hw_info(dcpmem_hw_info,
                                        dimm_size, power_budget, combine_type,
                                        "seq", "read")
  hw_rand_bandwidth = get_dcpmem_hw_info(dcpmem_hw_info,
                                         dimm_size, power_budget, combine_type,
                                         "rand", "read")
  hw_bw_per_gb = calc_hw_bw_per_gb(sequence_indicator,
                                   hw_seq_bandwidth,
                                   hw_rand_bandwidth,
                                   dcpmem_size_mb)
end
puts "#{hw_bw_per_gb}"
