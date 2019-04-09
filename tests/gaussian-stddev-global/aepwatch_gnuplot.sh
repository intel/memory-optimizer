#!/usr/bin/gnuplot

file="aep-watch.csv"
set out "img_aepwatch.png"

color1="#4169E1"
color2="#32CD32"
color3="#551A8B"
color4="#5B5B5B"
color5="#EE7600"
color6="#EE00EE"
color7="#33FFFF"
color8="#660000"

set terminal png
set terminal png size 4500,16000

set multiplot layout 12,1 #spacing 50,50

#X input is time
set xdata time
set timefmt "%s"

#change title font to 28pt
set key font ",28"

#X output time format
set format x "%H:%M:%S"

#set xtics axis rangelimited
set xtics scale 0.5 rotate by 25 offset -3,-0.5 font ",28"

#enable grid
set grid

set autoscale yfixmin

set datafile separator ";"

#change font size of diagram title
set title font ",38"

set ytics format "%.f MB" rotate by 25 offset 10,0 font ",28"
set title "bytes read (derived)"
plot \
file using 1:(column(3)/1024/1024)  every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:(column(14)/1024/1024) every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:(column(25)/1024/1024) every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:(column(36)/1024/1024) every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5


set title "bytes written (derived)"
plot \
file using 1:(column(4)/1024/1024)  every ::6 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:(column(15)/1024/1024) every ::6 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:(column(26)/1024/1024) every ::6 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:(column(37)/1024/1024) every ::6 title "DIMM 4" with lines linecolor rgb color4 linewidth 5

set ytics format "%.2f" rotate by 25 offset 10,0 font ",28"
set title "read hit ratio (derived)"
plot \
file using 1:5 every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:16 every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:27 every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:38 every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5
set title "write hit ratio (derived)"
plot \
file using 1:6 every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:17 every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:28 every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:39 every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5

set ytics format "%.f" rotate by 25 offset 10,0 font ",28"
set ytics rotate by 25 offset 10,0 font ",28"
set title "wdb merge percent (derived)"
plot \
file using 1:7 every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:18 every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:29 every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:40 every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5
#set autoscale y

set ytics format "%.fK" rotate by 25 offset 10,0 font ",28"
set title "sxp read ops (derived)"
plot \
file using 1:(column(8)/1000)  every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:(column(19)/1000) every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:(column(30)/1000) every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:(column(41)/1000) every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5


set title "sxp write ops (derived)"
plot \
file using 1:(column(9)/1000)  every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:(column(20)/1000) every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:(column(31)/1000) every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:(column(42)/1000) every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5
set title "read 64B ops received"
plot \
file using 1:(column(10)/1000) every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:(column(21)/1000) every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:(column(32)/1000) every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:(column(43)/1000) every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5


set title "write 64B ops received"
plot \
file using 1:(column(11)/1000) every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:(column(22)/1000) every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:(column(33)/1000) every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:(column(44)/1000) every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5


#set xlabel "time"
set title "ddrt read ops"
plot \
file using 1:(column(12)/1000) every ::6 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:(column(23)/1000) every ::6 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:(column(34)/1000) every ::6 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:(column(45)/1000) every ::6 title "DIMM 4" with lines linecolor rgb color4 linewidth 5


set title "ddrt write ops"
plot \
file using 1:(column(13)/1000) every ::8 title "DIMM 1" with lines linecolor rgb color1 linewidth 5, \
file using 1:(column(24)/1000) every ::8 title "DIMM 2" with lines linecolor rgb color2 linewidth 5, \
file using 1:(column(35)/1000) every ::8 title "DIMM 3" with lines linecolor rgb color3 linewidth 5, \
file using 1:(column(46)/1000) every ::8 title "DIMM 4" with lines linecolor rgb color4 linewidth 5
unset multiplot
