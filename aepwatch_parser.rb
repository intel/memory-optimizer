#!/usr/bin/ruby
require 'fileutils'
require_relative 'utility'

if __FILE__ == $0
  
  if ARGV.size < 1
    puts "Usage:"
    puts "Arg 0: directory path of aep-watch data"
    puts "Example:"
    puts "#{$0} /home/goodboy/test/today"
    return -1
  end

  aepwatch_csv_gnuplot = "aepwatch_gnuplot.sh"
  aepwatch_csv_file = "aep-watch.csv"
  
  #gnuplot phase
  search_dir=File.expand_path("#{ARGV[0]}")
  Dir.glob(File.join("**", aepwatch_csv_file), base:search_dir).each do |file|
    base_dir = File.join(search_dir, File.dirname(file))    
    puts "JOB #{$in_run}: found edp csv in #{base_dir}"
    FileUtils.cp(aepwatch_csv_gnuplot, base_dir, verbose:true)    
    gnuplot_proc = {
        :cmd => "/usr/bin/gnuplot " + File.join(base_dir, aepwatch_csv_gnuplot),
        :out => File.join(base_dir, "aepwatch_gnuplot.out"),
        :err => File.join(base_dir, "aepwatch_gnuplot.err"),
        :wait => true,
        :cwd => base_dir,
        :pid => nil,
    }
    new_proc(gnuplot_proc)
  end
end


