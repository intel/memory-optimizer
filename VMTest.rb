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
require 'fileutils'
require_relative "ProcVmstat"
require_relative "ProcStatus"
require_relative "ProcNumaMaps"
require_relative "emon"
require_relative "aepwatch"
require_relative "utility"

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
#           that kernel will migrate cold pages to PMEM. sys-refs will auto
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

  def spawn_qemu(type)
    env = {
      # "interleave" => (one_way ? @pmem_nodes : @all_nodes).join(','),
      "qemu_cmd" => @qemu_cmd,
      "qemu_smp" => @qemu_smp,
      "qemu_mem" => @qemu_mem,
      "qemu_ssh" => @qemu_ssh,
      "qemu_log" => @qemu_log,
    }
    qemu_nodes = @all_nodes
    qemu_nodes = @pmem_nodes if :one_way_migration == type
    qemu_nodes = @dram_nodes if :dram_one_way_migration == type
    qemu_nodes = @dram_nodes if :dram_baseline == type
    env["interleave"] = qemu_nodes.join(',')
    env["qemu_numactl"] = @scheme["qemu_numactl"] if @scheme["qemu_numactl"]

    cmd = File.join(@conf_dir, @qemu_script)
    log "env " + env.map { |k,v| "#{k}=#{v}" }.join(' ') + " " + cmd
    @qemu_pid = Process.spawn(env, cmd)
  end

  def wait_vm
    30.downto(1) do |i|
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
      #wait for huge memory allocation in VM
      wait_log_message(@workload_log, "Threads started", 1800)
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
    return if @scheme["one_way_migrate"] || @scheme["no_eatmem"]
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

    if @scheme["migrate_numactl"]
      migrate_numactl = "numactl " + @scheme["migrate_numactl"]
    else
      migrate_numactl = ""
    end

    cmd = "/usr/bin/time -v stdbuf -oL #{migrate_numactl} #{@project_dir}/#{@scheme['migrate_cmd']} --dram #{dram_percent} -c #{@migrate_config}"

    log cmd + " > " + @migrate_log
    File.open(@migrate_log, 'w') do |f| f.puts cmd end
    @migrate_pid = Process.spawn(cmd, [:out, :err]=>[@migrate_log, 'a'])

    sleep 5
    cmd = "pidof #{@migrate_script} > #{@migrate_pid_file}"
    system(*cmd)
    pid = File.read(@migrate_pid_file)
    if pid == ""
      @migrate_pid = -1
      puts "#{@scheme['migrate_cmd']} had exited!"
    else
      log "migrate_pid = #{pid}"
      @migrate_pid = pid.to_i
    end
  end

  def eat_mem_loop
    return if @scheme["one_way_migrate"] || @scheme["no_eatmem"]
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

  def run_type_migration?(run_type)
    run_type == :migration ||
    run_type == :one_way_migration ||
    run_type == :dram_one_way_migration
  end

  def run_one(run_type)
    path_params = @workload_params.map { |k,v| "#{k}=#{v}" }.join('#')
    path_params += '.' + "#{run_type}"
    path_params += '.' + @migrate_script if run_type_migration?(run_type)
    log_dir = File.join(@ratio_dir, path_params)
    log_file = File.join(log_dir, 'log')
    @workload_log = File.join(log_dir, @workload_script + ".log")
    @migrate_log  = File.join(log_dir, @migrate_script  + ".log")
    @qemu_log     = File.join(log_dir, File.basename(@qemu_script) + ".log")
    @usemem_pid_file = File.join(log_dir, "usemem.pid")
    @migrate_pid_file = File.join(log_dir, "migrate.pid")

    # Avoid this dependency in old RHEL
    #   require "FileUtils"
    #   FileUtils.mkdir_p(log_dir)
    system('mkdir', '-p', log_dir) # on host rootfs
    @logger = Logger.new(log_file)

    puts "less #{log_file}"
    log "Running test with params #{@workload_params} run_type=#{run_type}"

    spawn_qemu(run_type)
    wait_vm

    rsync_workload
    workload_pid = spawn_workload

    system("rm", "-f", @usemem_pid_file)
    system("rm", "-f", @migrate_pid_file)

    wait_workload_startup

    has_emon = File.exists?(@scheme["emon_base_dir"].to_s)
    has_aepwatch = File.exists?(@scheme["aepwatch_base_dir"].to_s)
    if has_emon
      emon_install_base_dir = @scheme["emon_base_dir"].to_s
      emon = Emon.new
      emon.set_emon_install(emon_install_base_dir)
      emon.set_event_file("/opt/intel/edp/Architecture Specific/CascadeLake/CLX-2S/clx-2s-events.txt")
      # emon.set_event_file("/opt/intel/edp/Architecture Specific/Skylake/SKX-2S/skx-2s-events.txt")
      emon.set_output_dir(log_dir)
      emon.start
    end
    if has_aepwatch
      aepwatch_install_base_dir = @scheme["aepwatch_base_dir"].to_s
      aepwatch = Aepwatch.new
      aepwatch.set_install(aepwatch_install_base_dir)
      aepwatch.set_output_dir(log_dir)
      aepwatch.start
    end

    if run_type_migration?(run_type)
      spawn_migrate
      eat_mem_loop
    elsif @dram_nodes.size * @ratio > @pmem_nodes.size # if cannot rely on interleaving in baseline test
      eat_mem
    end

    Process.wait workload_pid

    if has_emon
      emon.stop
    end

    if has_aepwatch
      aepwatch.stop
    end

    if run_type_migration?(run_type) && (@migrate_pid != -1)
      kill_wait @migrate_pid
    end

    usemem_pids = File.read(@usemem_pid_file).split rescue []
    usemem_pids.each do |pid| Process.kill 'KILL', pid.to_i rescue warn "WARNING: failed to kill usemem" end

    stop_qemu
    run_parser(log_dir, has_emon, has_aepwatch)
  end

  def run_parser(emon_log_dir, has_emon, has_aepwatch)
    #edp parser
    if has_emon
      ruby_edp_parser = File.join(@project_dir, "edp.rb")
      edp_parser = {
        :cmd => "/usr/bin/ruby " + ruby_edp_parser + " " + emon_log_dir,
        :out => File.join(emon_log_dir, "edp-parser.out"),
        :err => File.join(emon_log_dir, "edp-parser.err"),
        :wait => true,
        :pid => nil,
        :cwd => nil,
      }
      new_proc(edp_parser)
    end

    #aep-watch parser
    if has_aepwatch
      ruby_aepwatch_parser = File.join(@project_dir, "aepwatch_parser.rb")
      aepwatch_parser = {
        :cmd => "/usr/bin/ruby " + ruby_aepwatch_parser + " " + emon_log_dir,
        :out => File.join(emon_log_dir, "aepwatch_parser.out"),
        :err => File.join(emon_log_dir, "aepwatch_parser.err"),
        :wait => true,
        :pid => nil,
        :cwd => nil,
      }
      new_proc(aepwatch_parser)
    end
  end

  def adjust_nodes_pure(d, p, ratio)
    d = 0 if ratio == 999 # pure AEP
    p = 0 if ratio == 0   # pure DRAM
    [d, p]
  end

  # The baseline tests have to select d and p, so that d:p == 1:ratio
  # in order to make use of the interleaved NUMA policy.
  #
  # When baseline tests are enabled, in order to make results comparable,
  # the migration tests shall use the same NUMA nodes selected here.
  def adjust_nodes_baseline(d, p, ratio)

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

    [d, p]
  end

  def setup_nodes(ratio)
    # this func assumes d <= p
    d = @scheme["dram_nodes"].size
    p = @scheme["pmem_nodes"].size

    if @scheme["skip_baseline_run"] || @scheme["dram_one_way_migrate"]
      d, p = adjust_nodes_pure(d, p, ratio)
    else
      d, p = adjust_nodes_baseline(d, p, ratio)
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
    m["options"]["numa_nodes"] = gen_numa_nodes_conf unless @dram_nodes.empty?
    if @scheme['hugetlb']
      m["options"]["hugetlb"] = 1
      @scheme["one_way_migrate"] = 1 # no kernel hugetlb DRAM=>PMEM migration for now
    end
    if @scheme["one_way_migrate"]
      m["options"]["exit_on_exceeded"] = 1
      m["options"]["dram_percent"] = 100 / (@ratio + 1)
    end
    @ratio_dir = File.join(@time_dir, "ratio=#{@ratio}")
    @migrate_config = File.join(@ratio_dir, File.basename(@scheme['migrate_config']))
    system('mkdir', '-p', @ratio_dir)
    File.open(@migrate_config, 'w') do |file|
      file.write YAML.dump(m)
    end
  end

  # run type:
  #   one_way_migration
  #   migration
  #   baseline
  #   dram_baseline
  #   dram_one_way_migration
  def run_group
    @scheme["workload_params"].each do |params|
      @workload_params = params
      # run_one unless @scheme["skip_baseline_run"]
      run_one :baseline      unless @scheme["skip_baseline_run"]
      run_one :dram_baseline unless @scheme["skip_dram_baseline_run"] || @dram_nodes.empty?

      # should skip migrate?
      next if @dram_nodes.empty?
      next if @scheme["skip_migration_run"]

      # all migration start from here
      if @scheme["one_way_migrate"] then
        run_one :one_way_migration
      elsif @scheme["dram_one_way_migrate"] then
        run_one :dram_one_way_migration
      else
        run_one :migration
      end

    end
  end

  def run_all(scheme_file)
    @tests_dir = File.join @project_dir, 'tests'    
    @scheme = YAML.load_file(scheme_file)
    @conf_dir = File.dirname(File.realpath scheme_file)

    @host_workspace = File.join(@conf_dir, "log")
    @guest_workspace = "~/test"
    
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
