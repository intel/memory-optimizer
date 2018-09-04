CC = gcc
CXX = g++
CFLAGS = -Wall
CXXFLAGS = -Wall
LIB_SOURCE_FILES = lib/memparse.c lib/iomem_parse.c lib/page-types.c
CLASS_SOURCE_FILES = ProcIdlePages.cc ProcMaps.cc
CLASS_HEADER_FILES = $(CLASS_SOURCE_FILES:.cc=.h)

all: page-refs task-refs

page-refs: page-refs.c $(LIB_SOURCE_FILES)
	$(CC) -g $< $(LIB_SOURCE_FILES) -o $@ $(CFLAGS)

task-refs: task-refs.cc $(CLASS_SOURCE_FILES) $(CLASS_HEADER_FILES)
	$(CXX) -g $< $(CLASS_SOURCE_FILES) -o $@ $(CXXFLAGS) --std=c++14
