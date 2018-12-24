#!/usr/bin/ruby
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#          Yao Yuan <yuan.yao@intel.com>
#

require 'yaml'
require 'open3'
require 'logger'
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

  MIN_FREE_KB = 500<<10

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
    @guest_workspace = "~/test"
    @host_workspace = File.join(@tests_dir, "log")
  end

  def setup_params
    @qemu_script = @scheme["qemu_script"]   || "kvm.sh"
    @qemu_cmd    = @scheme["qemu_cmd"]      || "qemu-system-x86_64"
    @qemu_smp    =(@scheme["qemu_smp"]      || "32").to_s
    @qemu_mem    = @scheme["qemu_mem"]      || "128G"
    @qemu_ssh    =(@scheme["qemu_ssh"]      || "2222").to_s

    @transparent_hugepage = @scheme["transparent_hugepage"] || "never"

    @workload_script = @scheme["workload_script"]
    @migrate_script  = @scheme["migrate_cmd"].partition(' ')[0]
  end

  def setup_sys
    File.write("/sys/kernel/mm/transparent_hugepage/enabled", @transparent_hugepage)
    File.write("/proc/sys/vm/oom_kill_allocating_task", "1");
    File.write("/proc/sys/kernel/numa_balancing", "0")
    system("modprobe kvm_ept_idle")
  end

  def log(msg = "\n")
    @logger.info msg
  end

  def warn(msg = "\n")
    puts msg
    @logger.warn msg
  end

  def save_reproduce_files(scheme_file)
    system 'mkdir', '-p', @time_dir
    system 'cp', scheme_file, @time_dir
    system 'cp', File.join(@conf_dir, @qemu_script), @time_dir
    system 'cp', File.join(@conf_dir, @workload_script), @time_dir
    system 'cp', File.join(@conf_dir, @scheme['migrate_config']), @time_dir
  end

  def check_child(pid)
      begin
        # nil   => running
        # pid   => stopped
        return Process.waitpid(pid, Process::WNOHANG)
      rescue
        # false => no longer exist
        return false
      end
  end

  def kill_wait(pid)
    begin
      Process.kill 'KILL', pid
      sleep 1
      Process.wait pid
    rescue Errno::ESRCH, Errno::ECHILD
    end
  end

  def spawn_qemu(one_way)
    env = {
      "interleave" => (one_way ? @pmem_nodes : @all_nodes).join(','),
      "qemu_cmd" => @qemu_cmd,
      "qemu_smp" => @qemu_smp,
      "qemu_mem" => @qemu_mem,
      "qemu_ssh" => @qemu_ssh,
      "qemu_log" => @qemu_log,
    }

    cmd = File.join(@conf_dir, @qemu_script)
    log "env " + env.map { |k,v| "#{k}=#{v}" }.join(' ') + " " + cmd
    @qemu_pid = Process.spawn(env, cmd)
  end

  def wait_vm
    9.downto(1) do |i|
      sleep(i)
      # mkdir on guest rootfs
      output, status = Open3.capture2e("ssh", "-p", @qemu_ssh, "root@localhost", "mkdir -p #{@guest_workspace}")
      return if status.success?
      # show only unexpected error
      output.each_line do |line|
        # ssh_exchange_identification: read: Connection reset by peer
        # ssh_exchange_identification: Connection closed by remote host
        next if line =~ /^ssh_exchange_identification: /
        log line
      end
    end
    warn "failed to ssh VM"
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
           File.join(@conf_dir, @workload_script), "root@localhost:#{@guest_workspace}/"]
    log cmd.join(' ')
    system(*cmd)
  end

  def spawn_workload
    cmd = %W[ssh -p #{@qemu_ssh} root@localhost env]
    cmd += @workload_params.map do |k,v| "#{k}=#{v}" end
    cmd += %w[stdbuf -oL]
    cmd << File.join(@guest_workspace, @workload_script)
    log cmd.join(' ') + " > " + @workload_log
    return Process.spawn(*cmd, [:out, :err]=>[@workload_log, 'w'])
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
    warn "WARNING: timeout waiting for '#{msg}' in #{log}"
  end

  def read_qemu_rss
    proc_status = ProcStatus.new
    proc_status.load(@qemu_pid)
    if @scheme["hugetlb"]
      @qemu_rss_kb = proc_status["HugetlbPages"].to_i
    else
      @qemu_rss_kb = proc_status["VmRSS"].to_i
    end
    log "QEMU RSS: #{@qemu_rss_kb >> 10}M"
  end

  def show_dram_percent(proc_numa_maps)
    dram_rss_kb = 0
    @scheme["dram_nodes"].each do |nid|
      dram_rss_kb += (proc_numa_maps.numa_kb["N#{nid}"] || 0)
    end
    total_anon_kb = proc_numa_maps.numa_kb['anon'] + 1
    log "QEMU numa #{proc_numa_maps.total_numa_kb >> 10}M"
    log "QEMU anon #{total_anon_kb >> 10}M"
    log "QEMU RSS  #{@qemu_rss_kb >> 10}M"
    log "QEMU DRAM #{dram_rss_kb >> 10}M  percent #{100 * dram_rss_kb / total_anon_kb}%  target #{100 / (1 + @ratio)}%"
  end

  def show_qemu_placement
    proc_numa_maps = ProcNumaMaps.new
    proc_numa_maps.load(@qemu_pid)
    log
    log "QEMU anon pages distribution:"
    (@scheme["dram_nodes"] + @scheme["pmem_nodes"]).each do |nid|
      qemu_anon_kb = proc_numa_maps.numa_kb["N#{nid}"] || 0
      percent = 100 * qemu_anon_kb / (@qemu_rss_kb + 1)
      log "Node #{nid}: #{qemu_anon_kb >> 10}M  #{percent}%"
    end
    show_dram_percent(proc_numa_maps)
  end

  # In the below scenario,
  #   @dram_nodes = [0, 1]
  #   other_dram_nodes = (2..7)
  #   @ratio = 4
  # This calculates the target RSS for node 0/1 given the expected @ratio.
  # Anonymous page distribution across NUMA nodes:
  #      11,385,036       anon total
  #       1,324,180  11%  anon node 0
  #       1,291,004  11%  anon node 1
  #         182,088   1%  anon node 2
  #         182,388   1%  anon node 3
  #         277,508   2%  anon node 4
  #         182,464   1%  anon node 5
  #         182,452   1%  anon node 6
  #         182,340   1%  anon node 7
  #       1,026,100   9%  anon node 8
  #         949,632   8%  anon node 9
  #         929,948   8%  anon node 10
  #         929,552   8%  anon node 11
  #         950,232   8%  anon node 12
  #         932,740   8%  anon node 13
  #         932,852   8%  anon node 14
  #         929,556   8%  anon node 15
  def calc_target_rss_per_node(proc_numa_maps)
    other_dram_nodes = @scheme["dram_nodes"] - @dram_nodes
    other_dram_kb = 0
    other_dram_nodes.each do |nid| other_dram_kb += (proc_numa_maps.numa_kb["N#{nid}"] || 0) end
    dram_rss_kb = @qemu_rss_kb / (1 + @ratio)
    tt = (dram_rss_kb - other_dram_kb) / @dram_nodes.size
    tt = 0 if tt < 0
    tt
  end

  def eat_mem(is_squeeze = false)
    return if @scheme["one_way_migrate"]
    proc_vmstat = ProcVmstat.new
    proc_numa_maps = ProcNumaMaps.new
    proc_numa_maps.load(@qemu_pid)
    rss_per_node = calc_target_rss_per_node(proc_numa_maps)
    log
    log "Check eat DRAM memory"
    progress = false
    @dram_nodes.each do |nid|
      if is_squeeze
        # After rounds of migration, expect little free DRAM left, while QEMU
        # may occupied more DRAM than desired. Squeeze extra bytes to PMEM.
        free_kb = 0
      else
        # Initial call, expecting lots of free pages.
        # At this time, QEMU may well take fewer DRAM than target ratio.
        numa_vmstat = proc_vmstat.numa_vmstat[nid]
        free_kb = numa_vmstat['nr_free_pages']
        free_kb *= ProcVmstat::PAGE_SIZE >> 10
      end
      qemu_anon_kb = proc_numa_maps.numa_kb["N#{nid}"] || 0
      log "Node #{nid}: free #{free_kb >> 10}M  qemu #{qemu_anon_kb >> 10}M => #{rss_per_node >> 10}M"
      free_kb -= [free_kb, MIN_FREE_KB].min        # Linux will reserve some free memory
      free_kb /= 2    # eat memory step by step in a dynamic environment, to avoid OOM kill
      eat_kb = free_kb + qemu_anon_kb - rss_per_node
      eat_kb -= eat_kb >> 9  # account for 8/4096 page table pages
      progress = (spawn_usemem(nid, eat_kb) || progress)
    end
    show_dram_percent(proc_numa_maps)
    progress
  end

  def spawn_usemem(nid, kb)
    if kb < 1024
      warn "WARNING: not starting usemem due to negative kb = #{kb}" if kb < -(1000<<10)
      return
    end
    if @scheme['hugetlb']
      usemem_hugetlb = "--hugetlb --anonymous"
    else
      usemem_hugetlb = nil
    end
    cmd = "numactl --membind #{nid} usemem --detach --pid-file #{@usemem_pid_file} --sleep -1 --step 2m --mlock --prefault #{usemem_hugetlb} #{kb >> 10}m"
    log cmd
    system(*cmd.split)
  end

  def spawn_migrate
    dram_percent = 100 / (@ratio + 1)
    cmd = "/usr/bin/time -v stdbuf -oL #{@project_dir}/#{@scheme['migrate_cmd']} --dram #{dram_percent} -c #{@migrate_config}"
    log cmd + " > " + @migrate_log
    File.open(@migrate_log, 'w') do |f| f.puts cmd end
    @migrate_pid = Process.spawn(cmd, [:out, :err]=>[@migrate_log, 'a'])
  end

  def eat_mem_loop
    return if @scheme["one_way_migrate"]
    5.times do |i| eat_mem; sleep i end
    rounds = 2
    percent = 50
    10.times do
      break if wait_for_migration_progress(rounds, percent) == false
      9.times do |i| break unless eat_mem :squeeze; sleep 1 end
      rounds += 2 + (rounds / 4)
      percent = 1 + (percent / 2)
    end
    99.times do |i| break unless eat_mem :squeeze; sleep 1 end
  end

  def wait_for_migration_progress(rounds, percent)
    10.times do |i|
      sleep i * 10
      return false if check_child(@migrate_pid) == false
      count = 0
      File.open(@migrate_log).each do |line|
        if line =~ /^need to migrate: +[0-9,]+ +(\d+)% of /
          count += 1
          return true if count >= rounds
          return true if $1.to_i < percent
        end
      end
    end
    # wait at most 600 seconds
    return true
  end

  def run_one(should_migrate = false)
    path_params = @workload_params.map { |k,v| "#{k}=#{v}" }.join('#')
    path_params += '.' + @migrate_script if should_migrate
    log_dir = File.join(@ratio_dir, path_params)
    log_file = File.join(log_dir, 'log')
    @workload_log = File.join(log_dir, @workload_script + ".log")
    @migrate_log  = File.join(log_dir, @migrate_script  + ".log")
    @qemu_log     = File.join(log_dir, @qemu_script     + ".log")
    @usemem_pid_file = File.join(log_dir, "usemem.pid")

    # Avoid this dependency in old RHEL
    #   require "FileUtils"
    #   FileUtils.mkdir_p(log_dir)
    system('mkdir', '-p', log_dir) # on host rootfs
    @logger = Logger.new(log_file)

    puts "less #{log_file}"
    log "Running test with params #{@workload_params} should_migrate=#{should_migrate}"

    spawn_qemu(should_migrate && @scheme["one_way_migrate"])
    wait_vm

    rsync_workload
    workload_pid = spawn_workload

    system("rm", "-f", @usemem_pid_file)

    if should_migrate
      wait_workload_startup
      spawn_migrate
      eat_mem_loop
    elsif @dram_nodes.size * @ratio > @pmem_nodes.size # if cannot rely on interleaving in baseline test
      eat_mem
    end

    Process.wait workload_pid

    if should_migrate
      kill_wait @migrate_pid
    end

    usemem_pids = File.read(@usemem_pid_file).split rescue []
    usemem_pids.each do |pid| Process.kill 'KILL', pid.to_i rescue warn "WARNING: failed to kill usemem" end

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

  def gen_numa_nodes_conf
    numa_nodes = Hash.new
    @dram_nodes.each_with_index do |nid, i|
      numa_nodes[nid] = { "type" => "DRAM", "demote_to" => @pmem_nodes[i] }
    end
    @pmem_nodes.each_with_index do |nid, i|
      numa_nodes[nid] = { "type" => "PMEM", "promote_to" => @dram_nodes[i % @dram_nodes.size] }
    end
    numa_nodes
  end

  def save_migrate_yaml
    m = YAML.load_file File.join(@conf_dir, @scheme['migrate_config'])
    m["numa_nodes"] = gen_numa_nodes_conf unless @dram_nodes.empty?
    if @scheme['hugetlb']
      m["options"]["hugetlb"] = 1
      @scheme["one_way_migrate"] = 1 # no kernel hugetlb DRAM=>PMEM migration for now
    end
    if @scheme["one_way_migrate"]
      m["options"]["exit_on_exceeded"] = 1
      m["options"]["dram_percent"] = 100 / (@ratio + 1)
    else
      m["options"]["exit_on_stabilized"] = 3
    end
    @ratio_dir = File.join(@time_dir, "ratio=#{@ratio}")
    @migrate_config = File.join(@ratio_dir, File.basename(@scheme['migrate_config']))
    system('mkdir', '-p', @ratio_dir)
    File.open(@migrate_config, 'w') do |file|
      file.write YAML.dump(m)
    end
  end

  def run_group
    @scheme["workload_params"].each do |params|
      @workload_params = params
      run_one unless @scheme["skip_baseline_run"]
      run_one should_migrate: true unless (@dram_nodes.empty? || @scheme["skip_migration_run"])
    end
  end

  def run_all(scheme_file)
    @scheme = YAML.load_file(scheme_file)
    @conf_dir = File.dirname(File.realpath scheme_file)
    @time_dir = File.join(@host_workspace, Time.now.strftime("%F.%T"))
    setup_params
    setup_sys
    save_reproduce_files scheme_file
    @scheme["ratios"].each do |ratio|
      @ratio = ratio
      setup_nodes(ratio)
      save_migrate_yaml
      run_group
    end
  end

end
