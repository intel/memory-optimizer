#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#

class ProcStatus

  def load(pid)
    @map = Hash.new
    File.open("/proc/#{pid}/status").each do |line|
      key, val = line.chomp.split /:\t */
      @map[key] = val
    end
  end

  def [](key)
    @map[key]
  end

end
