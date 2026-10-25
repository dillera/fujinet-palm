# Desktop tests for the shared C code. FW points at a fujinet-firmware checkout.
FW ?= ../../fujinet-firmware
CFLAGS = -std=c89 -pedantic -Wall -Wextra -I../common
CXXFLAGS = -std=c++20 -Wall -I../common -I$(FW)/include -I$(FW)/lib -I$(FW)/lib/bus

all: fujibus_interop_test lobbyjson_test holdem_test fujipet_test
	./fujibus_interop_test
	./lobbyjson_test
	./holdem_test
	./fujipet_test
	python3 fujipet_server_test.py

fujibus.o: ../common/fujibus.c ../common/fujibus.h ../common/fn_types.h
	$(CC) $(CFLAGS) -c -o $@ $<

fujibus_interop_test: fujibus_interop_test.cpp fujibus.o $(FW)/lib/bus/rs232/FujiBusPacket.cpp
	$(CXX) $(CXXFLAGS) -o $@ fujibus_interop_test.cpp fujibus.o $(FW)/lib/bus/rs232/FujiBusPacket.cpp

lobbyjson_test: lobbyjson_test.c ../common/lobbyjson.c ../common/lobbyjson.h ../common/fn_types.h
	$(CC) $(CFLAGS) -o $@ lobbyjson_test.c ../common/lobbyjson.c

HOLDEM = ../apps/Holdem
holdem_test: holdem_test.c ../common/holdem.c ../common/holdem.h $(HOLDEM)/cards.c $(HOLDEM)/cards.h $(HOLDEM)/cardart.h ../common/fn_types.h
	$(CC) $(CFLAGS) -I$(HOLDEM) -o $@ holdem_test.c ../common/holdem.c $(HOLDEM)/cards.c

FUJIPET = ../apps/FujiPet
fujipet_test: fujipet_test.c $(FUJIPET)/pet.c $(FUJIPET)/pet.h $(FUJIPET)/petproto.c $(FUJIPET)/petproto.h ../common/fn_types.h
	$(CC) $(CFLAGS) -I$(FUJIPET) -o $@ fujipet_test.c $(FUJIPET)/pet.c $(FUJIPET)/petproto.c

clean:
	rm -f fujibus.o fujibus_interop_test lobbyjson_test holdem_test fujipet_test
