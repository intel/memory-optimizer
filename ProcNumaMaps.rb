class ProcNumaMaps

  attr_reader :numa_maps

  def load(pid)
    @numa_maps = Hash.new
    @numa_kb = nil
    File.open("/proc/#{pid}/numa_maps").each do |line|
      fields = line.chomp.split
      addr = fields.shift.hex
      mpol = fields.shift
      pairs = Hash.new
      fields.each do |field|
        key, val = field.split '='
        if val
          val = val.to_i if val =~ /^[0-9]+$/
          pairs[key] = val
        else
          # handle heap, stack
        end
      end
      @numa_maps[addr] = pairs
    end
  end

  def numa_kb
    @numa_kb ||= calc_numa_kb
  end

  def calc_numa_kb
    numa_kb = Hash.new
    @numa_maps.each do |k, v|
      pagesize = v["kernelpagesize_kB"]
      next unless pagesize
      v.each do |kk, vv|
        next if kk == "kernelpagesize_kB"
        next if String === vv
        numa_kb[kk] ||= 0
        numa_kb[kk] += vv * pagesize
      end
    end
    numa_kb
  end

end
