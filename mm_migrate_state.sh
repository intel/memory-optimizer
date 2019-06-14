#!/bin/bash

FILE="$1"

START_TIME=$(grep "Starting page table scans" $FILE  | head -n 1 | awk '{print $5" "$6}')
STOP_TIME=$(grep "End of migration" $FILE | tail -n 1 | awk '{print $4" "$5}')
echo ""
echo "[+]Migration time cost:"
echo "Start time: $START_TIME"
echo "Stop  time: $STOP_TIME"

echo ""
echo "[+]Migration state:"
grep "migrate successful" $FILE | sed "s/,//g" | awk '{SUM +=$3} END { SIZE=int(SUM/NR); print "migration_count: "NR "\navg_success_size: "SIZE " KB" " (" SIZE/1024 " MB)" }'
grep "speed" $FILE | sed "s/[(,]//g" | awk '{SUM += $9} END {SPEED=SUM/NR; print "avg_migration_speed: "SPEED " KB/s" " (" SPEED/1024  " MB/s)"}'
MP_WARNING=$(grep "WARNING: move_pages return:" $FILE | wc -l)
echo "WARNING: move_pages() count: " $MP_WARNING

echo ""
echo "[+]Final DRAM distribution:"
grep -e "|" -e "DRAM page distribution" $FILE | tail -n 11


echo ""
echo "[+]Anon DRAM curve:"
ANON_COUNT=$(grep "Anon DRAM" $FILE | wc -l)
echo "Total Anon DRAM line: $ANON_COUNT"
echo "Anon DRAM state:"
grep "Anon DRAM" $FILE



