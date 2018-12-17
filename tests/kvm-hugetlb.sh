#!/bin/bash

[[ $kernel ]] || kernel=$1
[[ $kernel ]] || kernel=(
	/c/linux/arch/x86/boot/bzImage
)

[[ $qemu_cmd ]] || qemu_cmd=qemu-system-x86_64
[[ $qemu_smp ]] || qemu_smp='cpus=32'
[[ $qemu_mem ]] || qemu_mem='128G'
[[ $qemu_ssh ]] || qemu_ssh='2222'
[[ $qemu_log ]] && qemu_log=file:$qemu_log
[[ $qemu_log ]] || qemu_log=stdio
[[ $interleave ]] && numactl="numactl --interleave=$interleave"

kvm=(
	$numactl
	$qemu_cmd
	-machine pc,nvdimm
	-cpu host
	-enable-kvm
	-kernel $kernel
	-smp $qemu_smp
	-numa node,nodeid=0,memdev=hgtlb0
	-m $qemu_mem
	-mem-prealloc
	-object memory-backend-file,id=hgtlb0,size=$qemu_mem,mem-path=/dev/hugepages
	-device virtio-net-pci,netdev=net0
	-netdev user,id=net0,hostfwd=tcp::$qemu_ssh-:22
	-no-reboot
	-serial $qemu_log
	-display none
	-monitor null
)

append=(
	ip=dhcp
	nfsroot=10.0.2.2:/os/ubuntu,v3,tcp,intr,rsize=524288
	root=/dev/nfs
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

echo "kvm=${kvm[@]}"
exec "${kvm[@]}" --append "${append[*]}"
