PREFIX := /usr/local
BIN := ${PREFIX}/bin
CXXFLAGS := -g -Wall -O2 -std=c++11 -MMD -Wno-unused-result

filter-test: ../AddressRangeFilter.cc filter-test.cc
	$(CXX) $< filter-test.cc -o $@ $(CXXFLAGS)

all: filter-test
	make filter-test

clean:
	rm -f *.o *.d filter-test
