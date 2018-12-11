#!/usr/bin/ruby

require_relative "../VMTest.rb"
require_relative "../ExclusiveRun.rb"

ExclusiveRun.new("/tmp/vm-tests.lock") do

  vm_test = VMTest.new
  vm_test.run_all(ARGV[0] || "scheme-sysbench-memory.yaml")

end
