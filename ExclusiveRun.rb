#!/usr/bin/ruby
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#

class ExclusiveRun
  def initialize(file)
    unless File.new(file, File::RDWR|File::CREAT).flock(File::LOCK_NB|File::LOCK_EX)
      puts "Failed to grab #{file} -- check parallel runs?"
      exit
    end

    yield

    File.delete file
  end
end
