#!/usr/bin/ruby
require 'fileutils'
require_relative 'utility'

class Edp
  attr_accessor :edp_data
  attr_accessor :edp_m_data
  attr_accessor :edp_v_data
  attr_accessor :edp_process_path
  attr_accessor :output_path

  def run
    cmd = [
      @edp_process_path,
      @edp_data,
      @edp_v_data,
      @edp_m_data,
      @output_path,
    ].join(' ')

    run_out = File.join(output_path, "edp.out")
    run_err = File.join(output_path, "edp.err")
    
    puts "Running cmd: #{cmd}"
    pid = Process.spawn(cmd,
                        :out => [run_out, 'w'],
                        :err => [run_err, 'w'])
  end
end


if __FILE__ == $0
  
  if ARGV.size < 1
    puts "Usage:"
    puts "Arg 0: directory path of emon data"
    puts "Example:"
    puts "#{$0} /home/goodboy/test/today"
    return -1
  end
  max_run = 32
  $in_run = 0
    
  edp_process_path = "/opt/intel/edp/process.sh"
  edp_data = "emon.dat"
  edp_m_data = "emon-m.dat"
  edp_v_data = "emon-v.dat"
  edp_data = "emon.dat"
  edp_csv_file = "__edp_socket_view_details.csv"
  edp_csv_gnuplot = "edp_gnuplot.sh"

  edp_array = Array.new(max_run)
  
  search_dir=File.expand_path("#{ARGV[0]}")
  Dir.glob(File.join("**", edp_data), base:search_dir).each do |file|

    if ($in_run < max_run)
      base_dir = File.join(search_dir, File.dirname(file))
      puts "JOB #{$in_run}: found emon data in #{base_dir}"

      edp_array[$in_run] = Edp.new
      edp_array[$in_run].edp_process_path = edp_process_path
      edp_array[$in_run].edp_data = File.join(base_dir, edp_data)
      edp_array[$in_run].edp_m_data = File.join(base_dir, edp_m_data)
      edp_array[$in_run].edp_v_data = File.join(base_dir, edp_v_data)
      edp_array[$in_run].output_path = base_dir
      edp_array[$in_run].run
      $in_run += 1
    else      
      Process.waitpid -1
      $in_run -= 1
    end
  end

  #wait remain worker processes
  while $in_run > 0 do
    Process.waitpid -1
    $in_run -= 1
  end

  #gnuplot phase
  Dir.glob(File.join("**", edp_csv_file), base:search_dir).each do |file|
    base_dir = File.join(search_dir, File.dirname(file))    
    puts "JOB #{$in_run}: found edp csv in #{base_dir}"
    FileUtils.cp(edp_csv_gnuplot, base_dir, verbose:true)    
    gnuplot_proc = {
        :cmd => "/usr/bin/gnuplot " + File.join(base_dir, edp_csv_gnuplot),
        :out => File.join(base_dir, "edp_gnuplot.out"),
        :err => File.join(base_dir, "edp_gnuplot.err"),
        :wait => true,
        :cwd => base_dir,
        :pid => nil,
    }
    new_proc(gnuplot_proc)
  end
end


