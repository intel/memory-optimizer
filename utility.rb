#!/usr/bin/ruby


#{
#  :cmd => @emon_bin + " -v",
#  :out => output_file("emon-v.dat"),
#  :err => output_file("emon-v.err"),
#  :wait => true,
#  :pid => nil,
#   :cwd => str
#},
def new_proc(item)
    return if item[:skip]

    item[:cwd] = FileUtils.getwd() unless item[:cwd]
    
    puts "Running cmd: #{item[:cmd]}"
    item[:pid] = Process.spawn(item[:cmd],
                               :out => [item[:out], 'w'],
                               :err => [item[:err], 'w'],
                               :chdir => item[:cwd])
    Process.wait item[:pid] if item[:wait]
end
