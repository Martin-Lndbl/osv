include ../common.gmk

CXXFLAGS=-O3 -g -Wall -std=c++23 -fPIC $(COMMON) -Iinclude/
SRCS=$(wildcard src/*.cc)
SRCS+=server_main.cc
OBJS=$(SRCS:.cc=.o)
DYN=$(SRCS:.cc=.so)
D=$(SRCS:.cc=.d)
LIBS=-lyaml-cpp -ltlx

.PHONY: module
module: kvstore.so

quiet = $(if $V, $1, @echo " $2"; $1)

kvstore.so: $(OBJS)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $(OBJS) $(LIBS), LINK $@)

clean:
	rm -f $(DYN) $(D) $(OBJS) kvstore.so kvstore-stripped.so
