#!/usr/bin/ruby

class Emon

  def initialize
    set_emon_install("/opt/intel/sep")
    set_output_dir(File.join(Dir.pwd, Time.now.strftime("emon-%F.%T")))
    @event_file = ""
  end

  def set_emon_install(emon_install)
    @emon_install  = emon_install
    @emon_bin = File.join(@emon_install, "bin64", "emon")
    @emon_ko_dir = File.join(@emon_install, "sepdk", "src")
  end

  def set_output_dir(output_dir)
    @output_dir = output_dir
    @log_file = File.join(@output_dir, "log")
  end

  def set_event_file(event_file)
    @event_file = event_file
  end

  def output_file(file)
    File.join(@output_dir, file)
  end

  def run(item)
    return if item[:skip]

    puts "Running cmd: #{item[:cmd]}"
    item[:pid] = Process.spawn(item[:cmd],
                               :out => [item[:out], 'w'],
                               :err => [item[:err], 'w'])
    Process.wait item[:pid] if item[:wait]
  end

  def load_kernel_module()
    cmds = [
      {
        :cmd => File.join(@emon_ko_dir, "rmmod-sep"),
        :out => output_file("rmmod-sep.dat"),
        :err => output_file("rmmod-sep.err"),
        :wait => true,
        :pid => nil,
      },
      {
        :cmd => File.join(@emon_ko_dir, "insmod-sep"),
        :out => output_file("insmod-sep.dat"),
        :err => output_file("insmod-sep.err"),
        :wait => true,
        :pid => nil,
      }
    ]

    cmds.each do |cmd| run(cmd) end
  end

  def start()
    cmds = [
      {
        :cmd => @emon_bin + " -v",
        :out => output_file("emon-v.dat"),
        :err => output_file("emon-v.err"),
        :wait => true,
        :pid => nil,
      },
      {
        :cmd => @emon_bin + " -M",
        :out => output_file("emon-m.dat"),
        :err => output_file("emon-m.err"),
        :wait => true,
        :pid => nil,
      },
      {
        :cmd => @emon_bin + " -i " + @event_file,
        :out => output_file("emon.dat"),
        :err => output_file("emon.err"),
        :wait => false,
        :pid => nil,
      }
    ]

    system("mkdir", "-p", @output_dir)
    load_kernel_module
    cmds.each do |cmd| run(cmd) end
  end

  def stop()
    cmd = {
      :cmd => "emon -stop",
      :out => output_file("stop.out"),
      :err => output_file("stop.err"),
      :wait => true,
      :pid => nil,
      }
    run(cmd)
  end
end


if __FILE__ == $0

  if ARGV.size < 3

    puts "Usage:"
    puts "Arg 0: install path of sep"
    puts "Arg 1: the Architecture Specific txt file path"
    puts "Arg 2: collection duration, in seconds"
    puts "Example:"
    puts "#{$0} /opt/intel/sep /opt/intel/edp/as/CascadeLake/CLX-2S/clx-2s-events.txt 10"

    return -1
  end

  emon = Emon.new
  emon.set_emon_install(ARGV[0])
  emon.set_event_file(ARGV[1])
  emon.start
  sleep ARGV[2].to_i
  emon.stop

  return 0
end
