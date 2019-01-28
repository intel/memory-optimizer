#!/usr/bin/ruby

require_relative 'utility'

class Aepwatch

  def initialize
    set_install("/opt/intel/ipmwatch")
    set_output_dir(File.join(Dir.pwd, Time.now.strftime("aepwatch-%F.%T")))
  end

  def set_install(install)
    @install  = install
    @start_bin = File.join(@install, "bin64", "ipmwatch")
    @stop_bin = File.join(@install, "bin64", "ipmwatch-stop")
  end

  def set_output_dir(output_dir)
    @output_dir = output_dir
  end

  def output_file(file)
    File.join(@output_dir, file)
  end

  def start()
    cmds = [
      {
        :cmd => @start_bin + " 1",
        :out => output_file("aep-watch.csv"),
        :err => output_file("aep-watch.err"),
        :wait => false,
        :pid => nil,
      },
    ]

    system("mkdir", "-p", @output_dir)

    cmds.each do |cmd| new_proc(cmd) end
  end

  def stop()
    cmd = {
      :cmd => @stop_bin,
      :out => output_file("aep-watch-stop.out"),
      :err => output_file("aep-watch-stop.err"),
      :wait => true,
      :pid => nil,
    }

    # meet sometime stop failure, so called 3 times here, just a
    # workaround
    new_proc(cmd)
    sleep(10)
    new_proc(cmd)
    sleep(10)
    new_proc(cmd)
  end
end


if __FILE__ == $0

  if ARGV.size < 2

    puts "Usage:"
    puts "Arg 0: install path of aepwatch"
    puts "Arg 1: output dir"
    puts "Example:"
    puts "#{$0} /opt/intel/ipmwatch /tmp/aep-watch"

    return -1
  end

  aepwatch = Aepwatch.new
  aepwatch.set_install(ARGV[0])
  aepwatch.set_output_dir(ARGV[1])
  aepwatch.start
  sleep ARGV[2].to_i
  aepwatch.stop

  return 0
end
