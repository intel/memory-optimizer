CC = gcc
CXX = g++
RM = rm
DEBUG_FLAGS = -g -fsanitize=address -static-libasan
DEBUG_FLAGS = -g -O3
CFLAGS = $(DEBUG_FLAGS) -Wall
CXXFLAGS = $(DEBUG_FLAGS) -Wall --std=c++11
LIB_SOURCE_FILES = lib/memparse.c lib/iomem_parse.c lib/page-types.c
TASK_REFS_SOURCE_FILES = Option.cc ProcIdlePages.cc ProcMaps.cc ProcVmstat.cc EPTMigrate.cc AddrSequence.cc \
			 MovePages.cc VMAInspect.cc EPTScan.cc BandwidthLimit.cc Numa.cc \
			 lib/debug.c lib/stats.h Formatter.h
TASK_REFS_HEADER_FILES = $(TASK_REFS_SOURCE_FILES:.cc=.h)
SYS_REFS_SOURCE_FILES = $(TASK_REFS_SOURCE_FILES) ProcPid.cc ProcStatus.cc Process.cc GlobalScan.cc Queue.h \
						  OptionParser.cc
SYS_REFS_HEADER_FILES = $(SYS_REFS_SOURCE_FILES:.cc=.h)

OBJS = sys-refs page-refs task-maps show-vmstat addr-seq task-refs pid-list
all: $(OBJS)
	[ ! -f ../Makefile ] || RP2D=t make -C ..
	[ -x ./update ] && ./update || true

sys-refs: sys-refs.cc $(SYS_REFS_SOURCE_FILES) $(SYS_REFS_HEADER_FILES)
	./get_version.sh
	$(CXX) $< $(SYS_REFS_SOURCE_FILES) -o $@ $(CXXFLAGS) -lnuma -pthread -lyaml-cpp

page-refs: page-refs.c $(LIB_SOURCE_FILES)
	$(CC) $< $(LIB_SOURCE_FILES) -o $@ $(CFLAGS)

task-refs: task-refs.cc $(TASK_REFS_SOURCE_FILES) $(TASK_REFS_HEADER_FILES)
	./get_version.sh
	$(CXX) $< $(TASK_REFS_SOURCE_FILES) -o $@ $(CXXFLAGS) -lnuma

task-maps: task-maps.cc ProcMaps.cc ProcMaps.h
	$(CXX) $< ProcMaps.cc -o $@ $(CXXFLAGS)

show-vmstat: show-vmstat.cc ProcVmstat.cc
	$(CXX) $< ProcVmstat.cc -o $@ $(CXXFLAGS) -lnuma

addr-seq: AddrSequence.cc AddrSequence.h
	$(CXX) AddrSequence.cc -o $@ $(CXXFLAGS) -DADDR_SEQ_SELF_TEST

pid-list: ProcPid.cc ProcPid.h ProcStatus.cc ProcStatus.h
	$(CXX) ProcPid.cc ProcStatus.cc -o $@ $(CXXFLAGS) -DPID_LIST_SELF_TEST

clean:
	$(RM) $(OBJS)
