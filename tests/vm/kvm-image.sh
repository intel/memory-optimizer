#!/bin/bash
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
	-smp $qemu_smp
	-m $qemu_mem
	-device e1000,netdev=net0
	-netdev user,id=net0,hostfwd=tcp::$qemu_ssh-:22
	-drive if=virtio,file=images/guest.raw,format=raw
	-display none
	-serial $qemu_log
	-monitor none
	-vnc :3 -k en-us
)
#echo "${kvm[@]}"
exec ${kvm[@]}

