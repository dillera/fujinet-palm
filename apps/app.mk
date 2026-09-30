# Shared build rules for a Palm app; each app's Makefile sets APP, NAME, CREATOR.
# Run inside the palmdev image (see the top-level Makefile).
COMMON  = ../../common
CC      = m68k-palmos-gcc
CFLAGS  = -O2 -Wall -palmos3.5 -I$(COMMON) -I.
SOURCES = $(APP).c $(COMMON)/fujibus.c $(COMMON)/fnlink.c

$(APP).prc: $(APP) resources.stamp
	build-prc -n $(NAME) -c $(CREATOR) -o $@ $(APP) *.bin

$(APP): $(SOURCES) $(APP)Rsc.h $(COMMON)/*.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

resources.stamp: $(APP).rcp $(APP)Rsc.h icon.bmp
	pilrc -q $(APP).rcp
	touch $@

clean:
	rm -f $(APP) $(APP).prc *.bin resources.stamp
