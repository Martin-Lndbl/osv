include ../common.gmk

TARGET=FASTT_LIB
CXXFLAGS=-O3 -g -Wall -std=c++17 -fPIC $(COMMON) 

.PHONY: module
module: pingpong.so

quiet = $(if $V, $1, @echo " $2"; $1)

pingpong.so: $(SRCS) $(TARGET)
	$(call quiet, $(CXX) $(CXXFLAGS) -shared -o $@ $(SRCS), LINK $@)

clean:
	rm -f pingpong.so pingpong.d pingpong-stripped.so
