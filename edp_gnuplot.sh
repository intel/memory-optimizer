#!/usr/bin/gnuplot

file="__edp_socket_view_details.csv"
set out "img_edp_socket_view_details.png"

color1="#4169E1"
color2="#32CD32"
color3="#551A8B"
color4="#5B5B5B"
color5="#EE7600"
color6="#EE00EE"
color7="#33FFFF"
color8="#660000"

set terminal png
set terminal png size 4500,24000

set multiplot layout 16,1 #spacing 50,50

#X input is time
set xdata time
set timefmt "%m/%d/%Y %H:%M:%S"

#change title font to 28pt
set key font ",28"

#X output time format
set format x "%H:%M:%S"

#set xtics axis rangelimited
set xtics scale 0.5 rotate by 25 offset -3,-0.5 font ",28"

#enable grid
set grid

set autoscale yfixmin

set datafile separator ","

#change font size of diagram title
set title font ",38"

##set autoscale y

set ytics format "%.f MB" rotate by 25 offset 10,0 font ",28"
set title "DDR and AEP read bandwidth"
plot \
file using 2:(column(156)) every ::2 title "DDR" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(196)) every ::2 title "AEP" with lines linecolor rgb color2 linewidth 5


set ytics format "%.f MB" rotate by 25 offset 10,0 font ",28"
set title "DDR and AEP write bandwidth"
plot \
file using 2:(column(158)) every ::2 title "DDR" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(198)) every ::2 title "AEP" with lines linecolor rgb color2 linewidth 5

set ytics format "%.f MB" rotate by 25 offset 10,0 font ",28"
set title "Total read bandwidth on socket 0"
plot \
file using 2:(column(353)) every ::2 title "DDR local read" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(355)) every ::2 title "DDR remote read" with lines linecolor rgb color2 linewidth 5,\
file using 2:(column(357)) every ::2 title "AEP local read" with lines linecolor rgb color3 linewidth 5,\
file using 2:(column(359)) every ::2 title "AEP remote read" with lines linecolor rgb color4 linewidth 5

set ytics format "%.f MB" rotate by 25 offset 10,0 font ",28"
set title "Total read bandwidth on socket 1"
plot \
file using 2:(column(354)) every ::2 title "DDR local read" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(356)) every ::2 title "DDR remote read" with lines linecolor rgb color2 linewidth 5,\
file using 2:(column(358)) every ::2 title "AEP local read" with lines linecolor rgb color3 linewidth 5,\
file using 2:(column(360)) every ::2 title "AEP remote read" with lines linecolor rgb color4 linewidth 5

#set autoscale y
set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "2LM NearMemory read/write"
plot \
file using 2:(column(166)) every ::2 title "AEP MemoryMode % of non-inclusive writes to near memory" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(168)) every ::2 title "AEP memorymode % of near memory cache read miss" with lines linecolor rgb color2 linewidth 5


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "RPQ read latency(ns)"
plot \
file using 2:(column(170)) every ::2 title "DDR RPQ read latency" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(202)) every ::2 title "AEP RPQ read latency" with lines linecolor rgb color2 linewidth 5


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "WPQ write latency(ns)"
plot \
file using 2:(column(172)) every ::2 title "DDR WPQ write latency" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(226)) every ::2 title "AEP WPQ write latency" with lines linecolor rgb color2 linewidth 5


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "Average entries in RPQ/in RPQ when not empty"
plot \
file using 2:(column(174)) every ::2 title "DDR average entries in RPQ" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(204)) every ::2 title "AEP average entries in RPQ" with lines linecolor rgb color2  linewidth 5,\
file using 2:(column(176)) every ::2 title "DDR average entries in RPQ when not empty" with lines linecolor rgb color3 linewidth 5,\
file using 2:(column(206)) every ::2 title "AEP average entries in RPQ when not empty" with lines linecolor rgb color4 linewidth 5


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "Cycles when RPQ is empty"
plot \
file using 2:(column(178)) every ::2 title "DDR cycles when RPQ is empty" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(208)) every ::2 title "AEP cycles when RPQ is empty" with lines linecolor rgb color2 linewidth 5


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "Cycles when RPQ has 1/10/20/40(36) or more entries"
plot \
file using 2:(column(180)) every ::2 title "DDR cycles when RPQ has 1 or more entries" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(182)) every ::2 title "DDR cycles when RPQ has 10 or more entries" with lines linecolor rgb color2 linewidth 5,\
file using 2:(column(184)) every ::2 title "DDR cycles when RPQ has 20 or more entries" with lines linecolor rgb color3 linewidth 5,\
file using 2:(column(186)) every ::2 title "DDR cycles when RPQ has 40 or more entries" with lines linecolor rgb color4 linewidth 5,\
file using 2:(column(210)) every ::2 title "AEP cycles when RPQ has 1 or more entries" with lines linecolor rgb color5 linewidth 5,\
file using 2:(column(212)) every ::2 title "AEP cycles when RPQ has 10 or more entries" with lines linecolor rgb color6 linewidth 5,\
file using 2:(column(214)) every ::2 title "AEP cycles when RPQ has 24 or more entries" with lines linecolor rgb color7 linewidth 5,\
file using 2:(column(216)) every ::2 title "AEP cycles when RPQ has 36 or more entries" with lines linecolor rgb color8 linewidth 5


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "Average time (dclk) RPQ empty/not empty"
plot \
file using 2:(column(188)) every ::2 title "DDR avg time (dclk) RPQ NOT empty" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(190)) every ::2 title "DDR avg time (dclk) RPQ empty" with lines linecolor rgb color2 linewidth 5,\
file using 2:(column(218)) every ::2 title "AEP avg time (dclk) RPQ NOT empty" with lines linecolor rgb color3 linewidth 5,\
file using 2:(column(220)) every ::2 title "AEP avg time (dclk) RPQ empty" with lines linecolor rgb color4 linewidth 5,\
file using 2:(column(242)) every ::2 title "AEP avg time (dclk) WPQ NOT empty" with lines linecolor rgb color5 linewidth 5,\
file using 2:(column(244)) every ::2 title "AEP avg time (dclk) WPQ empty" with lines linecolor rgb color6 linewidth 5,\


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "DDR average time with 40 or more/less than 40 entries"
plot \
file using 2:(column(192)) every ::2 title "average time with >= 40 entries" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(194)) every ::2 title "average time with < 40 entries" with lines linecolor rgb color2 linewidth 5,\


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "AEP average time (dclk) with less than 36 entries in RPQ / with less than 30 entires in WPQ /with 30 or more entries in WPQ"
plot \
file using 2:(column(224)) every ::2 title "AEP average time (dclk) with < 36 entries in RPQ" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(246)) every ::2 title "AEP average time (dclk) >= 30 entries in WPQ" with lines linecolor rgb color2 linewidth 5,\
file using 2:(column(250)) every ::2 title "AEP average time (dclk) with < 30 entries in WPQ" with lines linecolor rgb color2 linewidth 5,\

set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "AEP average entries in WPQ/in WPQ when NOT empty"
plot \
file using 2:(column(228)) every ::2 title "AEP average entries in WPQ" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(230)) every ::2 title "AEP average entries in WPQ when NOT empty" with lines linecolor rgb color2 linewidth 5,\


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "AEP cycles when WPQ is empty/has 1,10,20,30 or more entries"
plot \
file using 2:(column(236)) every ::2 title "AEP cycles when WPQ is empty" with lines linecolor rgb color1 linewidth 5,\
file using 2:(column(232)) every ::2 title "AEP cycles when WPQ has 1 or more entries" with lines linecolor rgb color2 linewidth 5,\
file using 2:(column(234)) every ::2 title "AEP cycles when WPQ has 10 or more entries" with lines linecolor rgb color3 linewidth 5,\
file using 2:(column(238)) every ::2 title "AEP cycles when WPQ has 20 or more entries" with lines linecolor rgb color4 linewidth 5,\
file using 2:(column(240)) every ::2 title "AEP cycles when WPQ has 30 or more entries" with lines linecolor rgb color5 linewidth 5,\


set ytics format "% h" rotate by 25 offset 10,0 font ",28"
set title "CHA % of cycles Fast asserted"
plot \
file using 2:(column(248)) every ::2 title "CHA % of cycles Fast asserted" with lines linecolor rgb color1 linewidth 5,\

     
unset multiplot
