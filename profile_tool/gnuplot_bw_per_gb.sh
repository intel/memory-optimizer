#!/usr/bin/env gnuplot

file="bw_per_gb_raw.log"

color1="#4169E1"
color2="#32CD32"
draw_linewidth=3

set terminal png
set terminal png size 1920,1080

set size 0.9, 0.9
set origin 0.05, 0.0

#change title font to
set key outside right font ",20"

set title "MBps-per-GB histogram" font ",24"

set xtics
set xrange [0:] reverse
set grid xtics
set xlabel "Ref count" font ",20" offset 0,-1
set xtics 0,1 offset 0,-0.5
set xtics font ",20"

set ytics
set ytics 0,25
set grid ytics
set ylabel "MBps-per-GB" font ",20" offset -7,0
set ytics format "%.f MBps/GB" font ",20" textcolor rgb color1 #offset 0.5,0.1
set ytics font ",20"

set y2tics
set y2tics 0,500
#set autoscale y2min
#set autoscale y2
#set grid y2tics
set ytics nomirror
set y2label "Memory size" font ",20" offset 4,0 rotate by 270
set y2tics format "%.f MB" font ",20" textcolor rgb color2 #offset 0,0

set boxwidth 0.6 relative
set style fill solid 1.0
set out "bw-per-gb-histogram.png"
plot \
    file using 2:(column(5)/1024.0/1024.0) title "Size" with boxes linecolor rgb color2 axis x1y2, \
    file using 2:(column(4)) title "MBps-per-GB" with linespoints linecolor rgb color1 linewidth draw_linewidth axis x1y1
