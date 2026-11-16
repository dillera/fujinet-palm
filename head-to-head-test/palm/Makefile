# Builds the three apps inside the palmdev image (the top-level Makefile runs
# this with the checkouts mounted):
#   /repos    fujinet-nio-workspace/repos (fujinet-nio-lib, and fujinet-nio for sbnio.h)
#   /classic  classic/fujinet-palm (its common/fujibus.c, the FujiBus codec)
#
#   FN H2H  ADHH  classic fujinet-firmware over the cradle
#   H2H     ANHH  fujinet-nio over the cradle (palmos-serial)
#   H2H SB  ANHB  fujinet-nio over the Springboard module (palmos-springboard)
#
# The NIO library is built here, into build/, from its own sources, with
# larger packet buffers than its targets ship with (PACKET_SERIAL,
# PACKET_SB) so the read-size sweep can try reads past the shipped size. The
# shipped read size (H2H_DEFAULT_CHUNK) is what the main tests use.
CC = m68k-palmos-gcc
SDK = 3.5
NIO_LIB ?= /repos/fujinet-nio-lib
SBNIO_INC ?= /repos/fujinet-nio/bridges/rp2350-springboard/include
CLASSIC ?= /classic/common
H2H_TARGET ?= http://fujinet.online:8199/h2h
# The app's own build, shown on its first line and in its report: commit7[-dirty] and "MM-DD HH:MMZ" (UTC). The
# top-level Makefile computes them on the Mac (git does not work in the container) and passes them here.
H2H_COMMIT ?= unknown
H2H_BUILT ?= unknown
B = build
OUT = $(abspath $(B))

PACKET_SERIAL = 4096
PACKET_SB = 8192
# The serial receive buffer (the library's default is 1024): a reply of up to
# PACKET_SERIAL must fit while the Palm reads it a byte at a time.
RX_SERIAL = 6144
# fn_read's largest body per exchange is FN_MAX_PACKET_SIZE - 22: 490 for
# palmos-serial's 512, 1002 for palmos-springboard's 1024.
CHUNK_SERIAL = 490
CHUNK_SB = 1002

CFLAGS = -O2 -Wall -palmos$(SDK) -I. '-DH2H_DEFAULT_TARGET="$(H2H_TARGET)"' \
         '-DH2H_COMMIT="$(H2H_COMMIT)"' '-DH2H_BUILT="$(H2H_BUILT)"'
APP_SRC = h2h_main.c suite.c clock.c
NIO_INC = -I$(NIO_LIB)/include -I$(NIO_LIB)/include/palmos

LIB_SERIAL = $(B)/lib-serial/fujinet-nio-palmos-serial.a
LIB_SB = $(B)/lib-sb/fujinet-nio-palmos-springboard.a

all: $(B)/FN_H2H.prc $(B)/H2H.prc $(B)/H2H_SB.prc $(B)/H2H_RAM.prc $(B)/H2H_NL.prc

$(B)/bin.stamp: h2h.rcp h2h_rsc.h icon.bmp
	mkdir -p $(B)/bin
	cd $(B)/bin && rm -f *.bin && pilrc -q -I $(CURDIR) $(CURDIR)/h2h.rcp
	touch $@

# Each app's name, creator and an 8 KB stack (build-prc's default is 4 KB,
# tight for the classic link's call chain under Palm OS 3's own calls).
define prc
	printf 'application { "%s" %s stack = 8192 }\n' $(2) $(3) > $(B)/$(1).def
	build-prc -o $@ $(B)/$(1).def $(B)/$(1)/h2h $(B)/bin/*.bin
endef

# ---------------------------------------------------------------- the library

SERIAL_FLAGS = -Iinclude/palmos -DFN_MAX_PACKET_SIZE=$(PACKET_SERIAL) -DFN_STREAM_WAKE_IDLE_MS=30000 \
	-DFN_PALM_RX_BUFFER=$(RX_SERIAL)
SB_FLAGS = -Iinclude/palmos -I$(SBNIO_INC) -DFN_MAX_PACKET_SIZE=$(PACKET_SB)

# The library's own makefile rebuilds on source changes only, so a change of
# flags starts its objects afresh.
define fresh_objects
	@mkdir -p $(B); echo '$(2)' | cmp -s - $(B)/$(1).flags || \
	  { rm -rf $(OUT)/obj-$(1) $(OUT)/lib-$(1); echo '$(2)' > $(B)/$(1).flags; }
endef

$(LIB_SERIAL): FORCE
	$(call fresh_objects,serial,$(SERIAL_FLAGS))
	$(MAKE) -s -C $(NIO_LIB) -f makefiles/build.mk TARGET=palmos-serial PROGRAM=fujinet-nio \
	  PALMOS_SDK_VERSION=$(SDK) OBJDIR=$(OUT)/obj-serial BUILDDIR=$(OUT)/lib-serial \
	  TARGET_CFLAGS_palmos-serial="$(SERIAL_FLAGS)" lib >/dev/null

$(LIB_SB): FORCE
	$(call fresh_objects,sb,$(SB_FLAGS))
	$(MAKE) -s -C $(NIO_LIB) -f makefiles/build.mk TARGET=palmos-springboard PROGRAM=fujinet-nio \
	  PALMOS_SDK_VERSION=$(SDK) OBJDIR=$(OUT)/obj-sb BUILDDIR=$(OUT)/lib-sb \
	  SBNIO_ABI_INCLUDE=-I$(SBNIO_INC) TARGET_CFLAGS_palmos-springboard="$(SB_FLAGS)" lib >/dev/null

# ---------------------------------------------------------------- the apps

$(B)/fn/h2h: $(APP_SRC) link_classic.c $(CLASSIC)/fujibus.c h2h.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(CLASSIC) '-DAPP_CREATOR_STR="ADHH"' '-DH2H_NAME="FN H2H"' \
	  -o $@ $(APP_SRC) link_classic.c $(CLASSIC)/fujibus.c

$(B)/fi/h2h: $(APP_SRC) link_nio.c probe_serial.c h2h.h $(LIB_SERIAL)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(NIO_INC) -DFN_MAX_PACKET_SIZE=$(PACKET_SERIAL) -DH2H_DEFAULT_CHUNK=$(CHUNK_SERIAL) \
	  '-DAPP_CREATOR_STR="ANHH"' '-DH2H_NAME="H2H"' \
	  -o $@ $(APP_SRC) link_nio.c probe_serial.c $(LIB_SERIAL) \
	  -Wl,--wrap=fn_transport_exchange,--wrap=fn_transport_exchange_buffers \
	  -Wl,--wrap=fn_channel_write_byte,--wrap=fn_channel_read_byte

$(B)/sb/h2h: $(APP_SRC) link_nio.c probe_sb.c h2h.h $(LIB_SB)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(NIO_INC) -I$(NIO_LIB)/src/platform/palmos -I$(SBNIO_INC) \
	  -DFN_MAX_PACKET_SIZE=$(PACKET_SB) -DH2H_DEFAULT_CHUNK=$(CHUNK_SB) -DH2H_SPRINGBOARD \
	  '-DAPP_CREATOR_STR="ANHB"' '-DH2H_NAME="H2H SB"' \
	  '-DH2H_RAM_NAME="H2H RAM"' '-DH2H_RAM_CREATOR_STR="ANHR"' \
	  -o $@ $(APP_SRC) link_nio.c probe_sb.c $(LIB_SB) -Wl,--wrap=fn_palmos_exchange

$(B)/FN_H2H.prc: $(B)/fn/h2h $(B)/bin.stamp Makefile
	$(call prc,fn,"FN H2H",ADHH)

$(B)/H2H.prc: $(B)/fi/h2h $(B)/bin.stamp Makefile
	$(call prc,fi,"H2H",ANHH)

$(B)/H2H_SB.prc: $(B)/sb/h2h $(B)/bin.stamp Makefile
	$(call prc,sb,"H2H SB",ANHB)

# The same app under its own name and creator, for RAM next to the module's
# ROM copy (HotSync it): the two runs show what fetching the code over the
# Springboard bus costs. The app labels its results by where it runs.
$(B)/sbr/h2h: $(APP_SRC) link_nio.c probe_sb.c h2h.h $(LIB_SB)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(NIO_INC) -I$(NIO_LIB)/src/platform/palmos -I$(SBNIO_INC) \
	  -DFN_MAX_PACKET_SIZE=$(PACKET_SB) -DH2H_DEFAULT_CHUNK=$(CHUNK_SB) -DH2H_SPRINGBOARD \
	  '-DAPP_CREATOR_STR="ANHR"' '-DH2H_NAME="H2H RAM"' \
	  -o $@ $(APP_SRC) link_nio.c probe_sb.c $(LIB_SB) -Wl,--wrap=fn_palmos_exchange

$(B)/H2H_RAM.prc: $(B)/sbr/h2h $(B)/bin.stamp Makefile
	$(call prc,sbr,"H2H RAM",ANHR)

# H2H NL: the same tests through the installed NioLib (fujinet-nio-lib's
# palmos-shared target, built apart: make niolib at the top), linking only its
# client archive. NioLib's packets are 1024 bytes on every transport, so reads
# stop at 1002. Its exchanges run inside NioLib, where no probe reaches.
NIOLIB_BUILD ?= $(NIO_LIB)/build/palmos-shared-$(SDK)
$(B)/nl/h2h: $(APP_SRC) link_nio.c probe_niolib.c h2h.h $(NIOLIB_BUILD)/libniolib-client.a
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(NIO_INC) -DFN_MAX_PACKET_SIZE=1024 -DH2H_DEFAULT_CHUNK=1002 -DH2H_NIOLIB \
	  '-DAPP_CREATOR_STR="ANHN"' '-DH2H_NAME="H2H NL"' \
	  '-DH2H_RAM_NAME="H2H NL RAM"' '-DH2H_RAM_CREATOR_STR="ANHM"' \
	  -o $@ $(APP_SRC) link_nio.c probe_niolib.c $(NIOLIB_BUILD)/libniolib-client.a

$(B)/H2H_NL.prc: $(B)/nl/h2h $(B)/bin.stamp Makefile
	$(call prc,nl,"H2H NL",ANHN)

clean:
	rm -rf $(B)

FORCE:
.PHONY: all clean FORCE
