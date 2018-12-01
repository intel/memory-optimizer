#!/usr/bin/ruby

require_relative "../VMTest.rb"

vm_test = VMTest.new
vm_test.run_all(ARGV[0] || "scheme-sysbench-memory.yaml")
