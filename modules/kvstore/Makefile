include ../common.gmk

CXXFLAGS=-O3 -g -Wall -std=c++20 -fPIC $(COMMON) -ltbb
LDFLAGS += -ltbb 
SRCS=$(wildcard *.cc)
OBJS=$(SRCS:.cc=.o)
LIBS=-lyaml-cpp -ltbb

.PHONY: module
module: kvstore.so

quiet = $(if $V, $1, @echo " $2"; $1)

kvstore.so: $(OBJS)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $(OBJS) $(LIBS), LINK $@)

clean:
	rm -f kvstore.so kvstore.d kvstore-stripped.so
