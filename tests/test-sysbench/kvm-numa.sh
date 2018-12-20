#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#

kernel=$1
[[ $kernel ]] || kernel=(
	/c/linux/arch/x86/boot/bzImage
)

[[ $qemu_smp ]] || qemu_smp='cpus=1'
[[ $qemu_mem ]] || qemu_mem='256G'

kvm=(
	#numactl -m 0 -- taskset -c 0-25
	qemu-system-x86_64
	-machine pc,nvdimm
	-cpu host
	-enable-kvm
	-kernel $kernel
	-smp $qemu_smp
	-m $qemu_mem
	# DRAM numa node
	-object memory-backend-ram,size=32G,host-nodes=0,policy=bind,id=node0
	-numa node,cpus=0-31,nodeid=0,memdev=node0
	# AEP numa node
	-object memory-backend-ram,size=64G,host-nodes=2,policy=bind,id=node1
	-numa node,cpus=32-63,nodeid=1,memdev=node1
	-net nic,vlan=0,macaddr=00:00:00:00:00:00,model=virtio
	-net user,vlan=0,hostfwd=tcp::2225-:22
	-boot order=nc
	-no-reboot
	-watchdog i6300esb
	-serial stdio
	-display none
	-monitor null
)

append=(
        ip=dhcp
	nfsroot=10.0.2.2:/os/ubuntu,v3,tcp,intr,rsize=524288
	root=/dev/nfs
	debug
	sched_debug
	apic=debug
	ignore_loglevel
	sysrq_always_enabled
	panic=10
	# prompt_ramdisk=0
	earlyprintk=ttyS0,115200
	console=ttyS0,115200
	console=tty0
	vga=normal
	# root=/dev/ram0
	rw
)

"${kvm[@]}" --append "${append[*]}"
