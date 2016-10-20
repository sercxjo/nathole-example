#!/usr/bin/make -f
# For make in apparent dir use:
#     mkdir bin-obj-dir ; cd bin-obj-dir
#     make -f path-to-source/Makefile
# Or if it is executable:
#     path-to-source/Makefile
# For cross-compiling add environ variable for example DEB_HOST_GNU_TYPE=arm-linux-gnueabihf
#
export SRC := $(dir $(lastword $(MAKEFILE_LIST)))# Directory of this Makefile 
vpath %.c $(SRC)
vpath %.h $(SRC)
vpath Makefile $(SRC)
ifneq ($(DEB_HOST_GNU_TYPE), $(DEB_BUILD_GNU_TYPE))# For cross-compiling
export CC=$(DEB_HOST_GNU_TYPE)-gcc
endif
CFLAGS=-std=gnu99 -Wall -g
BINS=stun_cli sig_srv
OBJS=$(patsubst %,%.o,$(BINS))

all: $(BINS)

clean:
	$(RM) $(OBJS)
	$(RM) $(BINS)

$(OBJS): nathole.h
