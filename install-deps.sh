#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#

has_cmd()
{
	command -v "$1" >/dev/null
}

debian_install()
{
	pkgs=(
		make
		gcc
		g++
		numactl
		libnuma-dev
		libyaml-cpp-dev
		ruby
		diffstat

		libasan4 # for gcc-7
	)
	sudo apt-get install "${pkgs[@]}"
}

rhel_install()
{
	pkgs=(
		gcc-c++
		libstdc++
		numactl
		numactl-libs
		numactl-devel
		yaml-cpp-devel
		diffstat

		libasan
	)
	sudo yum install "${pkgs[@]}"
}

if has_cmd apt-get; then
	debian_install
elif has_cmd yum; then
	rhel_install
else
	echo "unknown system"
fi

cat <<EOF
# tests/run-vm-tests.rb may need the "usemem" tool from

git clone https://git.kernel.org/pub/scm/linux/kernel/git/wfg/vm-scalability.git
cd vm-scalability && make usemem && sudo cp usemem /usr/local/bin/

# need latest sysbench for benchmarks

git clone https://github.com/akopytov/sysbench
cd sysbench && ./autogen.sh && ./configure && make && sudo cp src/sysbench /usr/local/bin/
EOF
