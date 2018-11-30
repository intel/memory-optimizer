#!/bin/bash

kernel=$1
[[ $kernel ]] || kernel=(
	/c/linux/arch/x86/boot/bzImage
)

kvm=(
	#numactl -m 0 -- taskset -c 0-25
	qemu-system-x86_64
	-machine pc,nvdimm
	-cpu host
	-enable-kvm
	-kernel $kernel
	-smp 64
	-m 256G
	-object memory-backend-file,size=128G,share=on,mem-path=/dev/shm/qemu_node0,id=tmpfs-node0
	-numa node,cpus=0-31,nodeid=0,memdev=tmpfs-node0
	-object memory-backend-file,size=128G,share=on,mem-path=/dev/dax1.0,align=128M,id=dax-node1
	-numa node,cpus=32-63,nodeid=1,memdev=dax-node1
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
