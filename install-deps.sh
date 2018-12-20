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
	apt-get install "${pkgs[@]}"
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
	yum install "${pkgs[@]}"
}

if has_cmd apt-get; then
	debian_install
elif has_cmd yum; then
	rhel_install
else
	echo "unknown system"
fi
