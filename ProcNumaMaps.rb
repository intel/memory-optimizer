#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#

class ProcNumaMaps

  attr_reader :numa_maps

  def load(pid)
    @numa_maps = Hash.new
    @numa_kb = nil
    File.open("/proc/#{pid}/numa_maps").each do |line|
      fields = line.chomp.split
      addr = fields.shift.hex
      mpol = fields.shift
      pairs = Hash.new
      fields.each do |field|
        key, val = field.split '='
        if val
          val = val.to_i if val =~ /^[0-9]+$/
          pairs[key] = val
        else
          # handle heap, stack
        end
      end
      @numa_maps[addr] = pairs
    end
  end

  def numa_kb
    @numa_kb ||= calc_numa_kb
  end

  def calc_numa_kb
    numa_kb = Hash.new
    @numa_maps.each do |k, v|
      pagesize = v["kernelpagesize_kB"]
      next unless pagesize
      v.each do |kk, vv|
        next if kk == "kernelpagesize_kB"
        next if String === vv
        numa_kb[kk] ||= 0
        numa_kb[kk] += vv * pagesize
      end
    end
    numa_kb
  end

  def total_numa_kb
    sum = 0
    numa_kb.each do |k, v|
      next unless k =~ /^N\d+$/
      sum += v;
    end
    sum
  end

  def show_numa_placement
    sum = total_numa_kb
    return unless sum && sum > 0

    numa_kb.each do |k, v|
      next unless k =~ /^N\d+$/
      percent = 100 * v / sum
      puts "#{k}  #{v >> 10}M  #{percent}%"
    end
  end

end
