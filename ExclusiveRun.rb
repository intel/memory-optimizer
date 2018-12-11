#!/usr/bin/ruby

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
