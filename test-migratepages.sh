#!/bin/bash

MEM=1g
DRAM_NODE=2
PMEM_NODE=8

pid_file1=$(mktemp)
pid_file2=$(mktemp)

echo "Start usemem $MEM in DRAM:"
numactl --membind=$DRAM_NODE usemem --detach --sleep 1000 --pid-file $pid_file1 $MEM

echo
echo "Start usemem $MEM in PMEM:"
numactl --membind=$PMEM_NODE usemem --detach --sleep 1000 --pid-file $pid_file2 $MEM

# double check page location
#./task-numa-maps.rb $pid1

pid1=$(<$pid_file1)
pid2=$(<$pid_file2)

echo
echo "DRAM => PMEM:"
time migratepages $pid1 $DRAM_NODE $PMEM_NODE
echo
echo "PMEM => DRAM:"
time migratepages $pid1 $PMEM_NODE $DRAM_NODE
time migratepages $pid2 $PMEM_NODE $DRAM_NODE
echo
echo "DRAM => PMEM:"
time migratepages $pid2 $DRAM_NODE $PMEM_NODE

rm $pid_file1
rm $pid_file2
kill $pid1
kill $pid2
