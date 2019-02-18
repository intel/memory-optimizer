PREFIX := /usr/local
BIN := ${PREFIX}/bin
LIBJEVENTS_DIR = ../../pmu-tools/jevents
PMEM2DRAM_DIR = ../../pmem2dram
CXXFLAGS := -g -Wall -O2 -std=c++11 -MMD -Wno-unused-result \
	-I${LIBJEVENTS_DIR} -I${PMEM2DRAM_DIR}
LDLIBS := -lnuma -lstdc++ -lm
OBJ := hmd.o cmsk.o pmu.o migration.o hmd-common.o hmd-config.o AddressRangeFilter.o \
	${PMEM2DRAM_DIR}/Numa.o
DEPS := $(patsubst %.o,%.d,${OBJ})
LIBJEVENTS = ${LIBJEVENTS_DIR}/libjevents.a

all: hmd
	[ -f ../Makefile ] && RHMD=t make -C ..

install: hmd
	install -d ${BIN}
	install -m 755 hmd ${BIN}

clean-jevents:
	make -C ${LIBJEVENTS_DIR} clean

clean: clean-jevents
	rm -f *.o *.d ${OBJ} $(DEPS) hmd

${LIBJEVENTS}:
	make -C ${LIBJEVENTS_DIR}

hmd: ${OBJ} ${LIBJEVENTS}

-include ${DEPS}
