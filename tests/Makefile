# Desktop tests for the shared C code. FW points at a fujinet-firmware checkout.
FW ?= ../../fujinet-firmware
CFLAGS = -std=c89 -pedantic -Wall -Wextra -I../common
CXXFLAGS = -std=c++20 -Wall -I../common -I$(FW)/include -I$(FW)/lib -I$(FW)/lib/bus

all: fujibus_interop_test
	./fujibus_interop_test

fujibus.o: ../common/fujibus.c ../common/fujibus.h ../common/fn_types.h
	$(CC) $(CFLAGS) -c -o $@ $<

fujibus_interop_test: fujibus_interop_test.cpp fujibus.o $(FW)/lib/bus/rs232/FujiBusPacket.cpp
	$(CXX) $(CXXFLAGS) -o $@ fujibus_interop_test.cpp fujibus.o $(FW)/lib/bus/rs232/FujiBusPacket.cpp

clean:
	rm -f fujibus.o fujibus_interop_test
