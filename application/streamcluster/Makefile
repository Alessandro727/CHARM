# Makefile for streamcluster

PREFIX=${PARSECDIR}/pkgs/kernels/streamcluster/inst/${PARSECPLAT}

TARGET=streamcluster
OBJS=streamcluster.o

CXXFLAGS := $(CXXFLAGS) -DENABLE_THREADS -pthread
OBJS += parsec_barrier.o crc.o

#BUILDTYPE := debug
BUILDTYPE := release

all: streamcluster

# --------------------------------------------------
# Shoal integration >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

# Path informations
ROOT := $(shell pwd)
BASE := $(ROOT)/../libshoal
SHOAL := $(BASE)/shoal

GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always)

OBJS += $(SHOAL)/src/misc.o $(SHOAL)/src/linux.o $(SHOAL)/src/shl.o

INC := \
	-I$(SHOAL)/inc

LIB := \
	-lnuma \
	-lpapi \
	-lpfm \
	-L$(SHOAL) -lshl \
	-L/usr/lib/x86_64-linux-gnu/ -llua5.2 \
       -lm -ldl

ifeq ($(BUILDTYPE),debug)
	CXXFLAGS +=-g -O0 -pg -DSHL_DEBUG
else
	CXXFLAGS += -O3
endif

CXXFLAGS += -std=gnu++0x

CXXFLAGS += $(INC) -DVERSION=\"$(GIT_VERSION)\" -fopenmp
LIBS += $(LIB)

# Helpers to build libshoal
# ------------------------------

LIBSHOAL := $(SHOAL)/libshl.so

# Recursively build libshoal
.PHONY: $(LIBSHOAL)
$(LIBSHOAL):
	$(MAKE) -C $(SHOAL) BUILDTYPE=$(BUILDTYPE)

sk_clean:
	$(MAKE) -C $(SHOAL) clean

# END Shoal integration <<<<<<<<<<<<<<<<<<<<<<<<<<<<
# --------------------------------------------------

.PHONY: streamcluster
streamcluster: $(LIBSHOAL) | $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LIBS) -o $(TARGET)

%.o : %.cpp
	$(CXX) $(CXXFLAGS) -c $<

%.o : %.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET)
	$(MAKE) -C $(SHLPREFIX) clean

install:
	mkdir -p $(PREFIX)/bin
	cp -f $(TARGET) $(PREFIX)/bin/$(TARGET)
