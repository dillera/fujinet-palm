FW ?= ../../../fujinet-firmware
CORE = $(FW)/lib/hotsync
CXXFLAGS = -std=c++20 -Wall -Wextra -g -I$(CORE) -I$(FW)/include -I.
CORE_SRCS = $(CORE)/Slp.cpp $(CORE)/PadpTransport.cpp $(CORE)/NetSyncTransport.cpp \
            $(CORE)/Dlp.cpp $(CORE)/DlpClient.cpp $(CORE)/PalmDatabase.cpp $(CORE)/HotSyncSession.cpp
SRCS = $(CORE_SRCS) HostLinks.cpp DirStorage.cpp

all: firstlight hotsync

firstlight: firstlight.cpp $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ firstlight.cpp $(SRCS)

hotsync: hotsync.cpp $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ hotsync.cpp $(SRCS)
