# Shared build rules for a Palm app; each app's Makefile sets APP, NAME, CREATOR,
# COMMON_EXTRA for any other common/*.c it links, and SOURCES_EXTRA for more
# of its own .c files. An app that sets NIO talks to FujiNet NIO through
# fujinet-nio-lib (NIO_LIB, built for its palm target) instead of the classic
# client in common/.
# Run inside the palmdev image (see the top-level Makefile).
COMMON  = ../../common
CC      = m68k-palmos-gcc
CFLAGS  = -O2 -Wall -palmos3.5 -I$(COMMON) -I. '-DAPP_CREATOR_STR="$(CREATOR)"'
ifdef NIO
NIO_LIB ?= ../../../fujinet-nio-workspace/repos/fujinet-nio-lib
# FN_MAX_PACKET_SIZE matches the library's palm target.
CFLAGS += -I$(NIO_LIB)/include -I$(NIO_LIB)/src/platform/palm/include -DFN_MAX_PACKET_SIZE=512
CLIENT  =
LIBS    = $(NIO_LIB)/build/fujinet-nio-palm.a
else
CLIENT  = $(COMMON)/fujibus.c $(COMMON)/fnlink.c
LIBS    =
endif
SOURCES = $(APP).c $(SOURCES_EXTRA) $(CLIENT) $(COMMON_EXTRA:%=$(COMMON)/%)

$(APP).prc: $(APP) resources.stamp
	build-prc -n $(NAME) -c $(CREATOR) -o $@ $(APP) *.bin

$(APP): $(SOURCES) $(LIBS) $(wildcard *.h) $(COMMON)/*.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LIBS)

resources.stamp: $(APP).rcp $(APP)Rsc.h icon.bmp
	pilrc -q $(APP).rcp
	touch $@

clean:
	rm -f $(APP) $(APP).prc *.bin resources.stamp
