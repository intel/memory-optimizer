#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018 Intel Corporation
#
# Authors: Fengguang Wu <fengguang.wu@intel.com>
#

refs_count_file=$1

[[ $refs_count_file ]] || {
	# try default output file
	[[ -f refs_count ]] && refs_count_file=refs_count
}

[[ $refs_count_file ]] || {
	echo "Usage: $0 refs_count_file"
	exit
}

plot() {
	gnuplot <<-EOF

	set xlabel "refs"
	set ylabel "pages"

	set logscale y

	set size 1
	set terminal pngcairo size ${width:-1280}, 800
	set terminal pngcairo size ${width:-1000}, 600
	set terminal pngcairo size ${width:-1280}, ${height:-800}

	set output "$refs_count_file.png"

	plot "$refs_count_file" using 1:2 with linespoints pt 7 ps 2 lw 2 lc rgbcolor "red" 	title "4k pages", \
	     "$refs_count_file" using 1:3 with linespoints pt 5 ps 2 lw 2 lc rgbcolor "blue"	title "2M pages", \
	     "$refs_count_file" using 1:4 with linespoints pt 3 ps 2 lw 2 lc rgbcolor "orange"	title "if 2M pages"

EOF
}

plot $refs_count_file
echo "feh $refs_count_file.png"
