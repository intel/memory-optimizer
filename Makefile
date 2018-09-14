CC = gcc
CXX = g++
CFLAGS = -Wall
CXXFLAGS = -lnuma -Wall --std=c++14
LIB_SOURCE_FILES = lib/memparse.c lib/iomem_parse.c lib/page-types.c
CLASS_SOURCE_FILES = ProcIdlePages.cc ProcMaps.cc ProcVmstat.cc Migration.cc
CLASS_HEADER_FILES = $(CLASS_SOURCE_FILES:.cc=.h)

all: page-refs task-refs task-maps show-vmstat

page-refs: page-refs.c $(LIB_SOURCE_FILES)
	$(CC) -g $< $(LIB_SOURCE_FILES) -o $@ $(CFLAGS)

task-refs: task-refs.cc $(CLASS_SOURCE_FILES) $(CLASS_HEADER_FILES) lib/debug.c lib/debug.h lib/stats.h
	$(CXX) -g $< $(CLASS_SOURCE_FILES) lib/debug.c -o $@ $(CXXFLAGS)
	[ -x ./update ] && ./update

task-maps: task-maps.cc ProcMaps.cc ProcMaps.h
	$(CXX) -g $< ProcMaps.cc -o $@ $(CXXFLAGS)

show-vmstat: show-vmstat.cc ProcVmstat.cc
	$(CXX) -g $< ProcVmstat.cc -o $@ $(CXXFLAGS)
