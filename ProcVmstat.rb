#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#

class ProcVmstat

  PAGE_SIZE = `getconf PAGESIZE`.to_i rescue 4096

  def load_hash(file)
    hash = Hash.new
    File.open(file).each_line do |line|
      key, val = line.split
      hash["#{key}"] = val.to_i if val != nil
    end
    hash
  end

  def vmstat
    @vmstat ||= load_hash("/proc/vmstat")
  end

  def numa_vmstat
    @numa_vmstat || load_numa_vmstat
  end

  def load_numa_vmstat
    @numa_vmstat = Array.new
    Dir.glob("/sys/devices/system/node/node*/vmstat") do |file|
      nid = file[29..-1].to_i
      @numa_vmstat[nid] = load_hash(file)
    end
    @numa_vmstat
  end

end
