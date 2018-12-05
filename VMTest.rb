#!/usr/bin/ruby

require 'yaml'
require_relative "ProcVmstat"
require_relative "ProcStatus"
require_relative "ProcNumaMaps"

# Basic test scheme:
#
# baseline run:
#         - run qemu on interleaved DRAM+PMEM nodes
#         - run workload in qemu
#
# migrate run:
#         - run qemu on interleaved DRAM+PMEM nodes
#         - run workload in qemu
#         - run usemem to consume DRAM pages
#         - run sys-refs to migrate hot pages to DRAM, creating LRU pressure so
#           that kernel will migrate cold pages to AEP.  sys-refs will auto
#           exit when all hot pages are roughly in DRAM with this patch.

class VMTest

  attr_accessor :transparent_hugepage
  attr_accessor :qemu_script
  attr_accessor :workload_script
  attr_accessor :vm_workspace

  attr_accessor :dram_nodes
  attr_accessor :pmem_nodes

  attr_accessor :qemu_smp
  attr_accessor :qemu_mem
  attr_accessor :qemu_ssh

  def initialize
    @project_dir = __dir__
    @tests_dir = File.join @project_dir, 'tests'
    @transparent_hugepage = 0
    @qemu_script = "kvm.sh"
    @guest_workspace = "~/test"
    @host_workspace = File.join(@tests_dir, "log")
    @qemu_ssh = "2222"
  end

  def setup_sys
    File.write("/sys/kernel/mm/transparent_hugepage/enabled", @transparent_hugepage)
    File.write("/proc/sys/kernel/numa_balancing", @numa_balancing)
    system("modprobe kvm_ept_idle")
  end

  def kill_wait(pid)
      Process.kill 'KILL', pid
      sleep 1
      Process.wait pid, Process::WNOHANG
  end

  def spawn_qemu
    env = {
      "interleave" => @all_nodes.join(','),
      "qemu_smp" => @qemu_smp,
      "qemu_mem" => @qemu_mem,
      "qemu_ssh" => @qemu_ssh,
      "qemu_log" => @qemu_log,
    }

    cmd = File.join(@tests_dir, @qemu_script)
    puts "env " + env.map { |k,v| "#{k}=#{v}" }.join(' ') + " " + cmd
    @qemu_pid = Process.spawn(env, cmd)
  end

  def wait_vm
    9.downto(1) do |i|
      sleep(i)
      # mkdir on guest rootfs
      system("ssh", "-p", @qemu_ssh, "root@localhost", "mkdir -p #{@guest_workspace}") && return
    end
    puts "failed to ssh VM"
    Process.kill 'KILL', @qemu_pid
    exit 1
  end

  def stop_qemu
    # record rss in baseline run to guide eat_mem() in next migration run
    #
    # If a baseline run will need memory adjustment by calling usemem (due to
    # no enough NUMA nodes to interleave), we can arrange an initial run on
    # pure DRAM or PMEM, which requires no memory adjustment.
    read_qemu_rss
    show_qemu_placement

    # QEMU may not exit on halt
    if system("ssh", "-p", @qemu_ssh, "root@localhost", "/sbin/reboot")
      Process.wait @qemu_pid
    else
      sleep 4
      kill_wait @qemu_pid
    end
  end

  def rsync_workload
    cmd = ["rsync", "-a", "-e", "ssh -p #{@qemu_ssh}",
           File.join(@tests_dir, @workload_script), "root@localhost:#{@guest_workspace}/"]
    puts cmd.join(' ')
    system(*cmd)
  end

  def spawn_workload
    cmd = %W[ssh -p #{@qemu_ssh} root@localhost env]
    cmd += @workload_params.map do |k,v| "#{k}=#{v}" end
    cmd << File.join(@guest_workspace, @workload_script)
    puts cmd.join(' ') + " > " + @workload_log
    @workload_pid = Process.spawn(*cmd, [:out, :err]=>[@workload_log, 'w'])
  end

  def wait_workload_startup
    if @workload_script =~ /sysbench/
      wait_log_message(@workload_log, "Threads started")
      # sysbench has allocated all memory at this point, so RSS here can be
      # immediately used by eat_mem(). This avoids dependency on some previous
      # run to tell qemu RSS.
      read_qemu_rss
    else
      sleep 5
    end
  end

  def wait_log_message(log, msg, seconds = 300)
    seconds.times do
      sleep 1
      return if File.read(log).include? msg
    end
    puts "WARNING: timeout waiting for '#{msg}' in #{log}"
  end

  def read_qemu_rss
    proc_status = ProcStatus.new
    proc_status.load(@qemu_pid)
    @qemu_rss_kb = proc_status["VmRSS"].to_i
    puts "QEMU RSS: #{@qemu_rss_kb >> 10}M"
  end

  def show_qemu_placement
    proc_numa_maps = ProcNumaMaps.new
    proc_numa_maps.load(@qemu_pid)
    @dram_nodes.each do |nid|
      qemu_anon_kb = proc_numa_maps.numa_kb["N#{nid}"] || 0
      percent = 100 * qemu_anon_kb / (@qemu_rss_kb + 1)
      puts "Node #{nid}: #{qemu_anon_kb >> 10}M  #{percent}%"
    end
  end

  def eat_mem
    rss_per_node = @qemu_rss_kb / (1 + @ratio) / @dram_nodes.size
    proc_vmstat = ProcVmstat.new
    proc_numa_maps = ProcNumaMaps.new
    proc_numa_maps.load(@qemu_pid)
    @usemem_pids = []
    @dram_nodes.each do |nid|
      numa_vmstat = proc_vmstat.numa_vmstat[nid]
      free_kb = numa_vmstat['nr_free_pages'] + numa_vmstat['nr_inactive_file']
      free_kb *= ProcVmstat::PAGE_SIZE >> 10
      qemu_anon_kb = proc_numa_maps.numa_kb["N#{nid}"] || 0
      puts "Node #{nid}: free #{free_kb >> 10}M  qemu #{qemu_anon_kb >> 10}M => #{rss_per_node >> 10}M"
      spawn_usemem(nid, free_kb + qemu_anon_kb - rss_per_node)
    end
  end

  def spawn_usemem(nid, kb)
    if kb < 0
      puts "WARNING: not starting usemem due to negative kb = #{kb}" if kb < -(1000<<10)
      return
    end
    cmd = "numactl --membind #{nid} usemem --sleep -1 --step 2m --mlock --prefault #{kb >> 10}m"
    puts cmd
    @usemem_pids << Process.spawn(cmd)
  end

  def spawn_migrate
    cmd = "stdbuf -oL #{@project_dir}/#{@scheme['migrate_cmd']} -c #{@project_dir}/#{@scheme['migrate_config']}"
    puts cmd + " > " + @migrate_log
    @migrate_pid = Process.spawn(cmd, [:out, :err]=>[@migrate_log, 'w'])
  end

  def run_one(should_migrate = false)
    path_params = @workload_params.map { |k,v| "#{k}=#{v}" }.join('#')
    path_params += '.' + @migrate_script if should_migrate
    log_dir = File.join(@host_workspace, @time_dir, "ratio=#{@ratio}", path_params)
    @workload_log = File.join(log_dir, @workload_script + ".log")
    @migrate_log  = File.join(log_dir, @migrate_script  + ".log")
    @qemu_log     = File.join(log_dir, @qemu_script     + ".log")

    puts '-' * 80
    puts "#{Time.now}  Running test with params #{@workload_params} should_migrate=#{should_migrate}"

    # Avoid this dependency in old RHEL
    #   require "FileUtils"
    #   FileUtils.mkdir_p(log_dir)
    system('mkdir', '-p', log_dir) # on host rootfs

    spawn_qemu
    wait_vm

    rsync_workload
    spawn_workload

    if should_migrate
      wait_workload_startup
      eat_mem
      spawn_migrate
    elsif @dram_nodes.size * @ratio > @pmem_nodes.size # if cannot rely on interleaving in baseline test
      eat_mem
    end

    Process.wait @workload_pid

    if should_migrate
      kill_wait @migrate_pid
      @usemem_pids.each do |pid| kill_wait pid end
    end

    stop_qemu
  end

  def setup_nodes(ratio)
    # this func assumes d <= p
    d = @scheme["dram_nodes"].size
    p = @scheme["pmem_nodes"].size

    # d, p, ratio: 2, 4, 4 => 1, 4, 4
    if d * ratio > p
      d = p / ratio   # pure PMEM if (ratio > p)
    end

    # In 2 socket system w/o fake NUMA, there will be 2 DRAM nodes and 2 PMEM nodes.
    # To use all DRAM DIMM bandwidth in 1:2 and 1:4 ratios, interleave mempolicy won't
    # help us get the target DRAM:PMEM ratio. However can still set min_dram_nodes=2
    # and let usemem to adjust memory distribution to the specified ratio.
    if @scheme["min_dram_nodes"]
      if d < @scheme["min_dram_nodes"]
         d = @scheme["min_dram_nodes"]
      end
    end

    # d, p, ratio: 2, 4, 1 => 2, 2, 1
    if d > 0
      p = d * ratio   # pure DRAM if (ratio == 0)
    end

    # When there are no enough NUMA nodes, performance can more comparable for
    #   d p ratio
    #   1 1 1
    #   1 2 2
    #   1 4 4
    # than with
    #   2 2 1
    #   2 4 2
    #   1 4 4
    # No longer necessary when numa=fake can create enough nodes, or when
    # usemem can help squeeze memory distribution.
    if @scheme["single_dram_node"]
      # d, p, ratio: 2, 2, 1 => 1, 1, 1
      if d > 1
        p /= d
        d = 1
      end
    end

    # dram_nodes/pmem_nodes in scheme are physically available nodes
    # dram_nodes/pmem_nodes in class are to be used in test runs
    @dram_nodes = @scheme["dram_nodes"][0, d]
    @pmem_nodes = @scheme["pmem_nodes"][0, p]
    @all_nodes = @dram_nodes + @pmem_nodes
  end

  def run_group
    @scheme["workload_params"].each do |params|
      @workload_params = params
      run_one
      run_one should_migrate: true unless @dram_nodes.empty?
    end
  end

  def run_all(config_file)
    @scheme = YAML.load_file(config_file)
    @workload_script = @scheme["workload_script"]
    @migrate_script = @scheme["migrate_cmd"].partition(' ')[0]
    @qemu_script = @scheme["qemu_script"] if @scheme["qemu_script"]
    @time_dir = Time.now.strftime("%F.%T")
    @scheme["ratios"].each do |ratio|
      @ratio = ratio
      setup_nodes(ratio)
      run_group
    end
  end

end
