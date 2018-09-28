CC = gcc
CXX = g++
DEBUG_FLAGS = -g -fsanitize=address
CFLAGS = $(DEBUG_FLAGS) -Wall
CXXFLAGS = $(DEBUG_FLAGS) -Wall --std=c++14
LIB_SOURCE_FILES = lib/memparse.c lib/iomem_parse.c lib/page-types.c
CLASS_SOURCE_FILES = Option.cc ProcIdlePages.cc ProcMaps.cc ProcVmstat.cc Migration.cc AddrSequence.cc
CLASS_HEADER_FILES = $(CLASS_SOURCE_FILES:.cc=.h)

all: page-refs task-maps show-vmstat addr-seq task-refs

page-refs: page-refs.c $(LIB_SOURCE_FILES)
	$(CC) $< $(LIB_SOURCE_FILES) -o $@ $(CFLAGS)

task-refs: task-refs.cc $(CLASS_SOURCE_FILES) $(CLASS_HEADER_FILES) lib/debug.c lib/debug.h lib/stats.h
	$(CXX) $< $(CLASS_SOURCE_FILES) lib/debug.c -o $@ $(CXXFLAGS) -lnuma
	[ -x ./update ] && ./update || true

task-maps: task-maps.cc ProcMaps.cc ProcMaps.h
	$(CXX) $< ProcMaps.cc -o $@ $(CXXFLAGS)

show-vmstat: show-vmstat.cc ProcVmstat.cc
	$(CXX) $< ProcVmstat.cc -o $@ $(CXXFLAGS) -lnuma

addr-seq: AddrSequence.cc AddrSequence.h
	$(CXX) AddrSequence.cc -o $@ $(CXXFLAGS) -DADDR_SEQ_SELF_TEST
