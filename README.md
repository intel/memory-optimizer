DISCONTINUATION OF PROJECT

This project will no longer be maintained by Intel.

Intel has ceased development and contributions including, but not limited to, maintenance, bug fixes, new releases, or updates, to this project.  

Intel no longer accepts patches to this project.

If you have an ongoing need to use this project, are interested in independently developing it, or would like to maintain patches for the open source software community, please create your own fork of this project.  

Contact: webadmin@linux.intel.com
MemoryOptimizer -- hot page accounting and migration daemon
===========================================================

PREREQUISITE
------------

### Hardware

- Intel Skylake/Cascade Lake processors
- NVDIMM persistent memory

### Kernel

- LKML discussions:

	[[RFC][PATCH v2 00/21] PMEM NUMA node and hotness accounting/migration](https://lkml.org/lkml/2018/12/26/138)

- git repo:

		git clone https://git.kernel.org/pub/scm/linux/kernel/git/wfg/linux.git
		git checkout -B ept-idle-v2-4.20 origin/ept-idle-v2-4.20

- kconfig:

		CONFIG_NUMA=y
		CONFIG_MEM_SOFT_DIRTY=y
		CONFIG_PROC_PAGE_MONITOR=y
		CONFIG_IDLE_PAGE_TRACKING=y
		CONFIG_KVM=m
		CONFIG_KVM_EPT_IDLE=m

### OS packages

Debian/Ubuntu and RHEL users can run this script to install packages:

	./install-deps.sh

### Build

	make

TOOLS
-----

- page-refs: PFN based page table scan
- task-refs: page table scan and migration for one process/VM
- sys-refs:  page table scan and migration for a set of process/VMs

sys-refs is most powerful and can run as daemon to manage hot/cold page
placement on DRAM/PMEM nodes.
