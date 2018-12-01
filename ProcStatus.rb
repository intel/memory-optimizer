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
