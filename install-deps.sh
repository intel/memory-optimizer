#!/bin/bash

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
	)
	apt-get install "${pkgs[@]}"
}

rhel_install()
{
	pkgs=(
		gcc-c++.x86_64
		libstdc++.x86_64
		numactl.x86_64
		numactl-libs.x86_64
		numactl-devel.x86_64
		yaml-cpp-devel.x86_64
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
