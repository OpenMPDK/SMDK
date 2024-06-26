PCM_DIR=pcm/build/

CC = g++
CFLAGS = -Wall -Wextra -std=c++17

TARGET = tierd
SOURCES = src/RemainBandwidthAgent.cpp \
		  src/MaxBandwidthAgent.cpp \
		  src/NodeSaturationAgent.cpp \
		  src/FallbackOrderAgent.cpp \
		  src/AgentFactory.cpp \
		  src/BandwidthLoader.cpp \
		  src/SignalHandler.cpp \
		  src/InputParser.cpp \
		  src/SystemInfo.cpp \
		  src/Logger.cpp \

ARCH := $(shell uname -m)
ifeq ($(ARCH), x86_64)
    VENDOR := $(shell lscpu | grep 'Vendor ID' | awk '{print $$NF}' | head -n 1)
    ifeq ($(VENDOR), GenuineIntel)
        override LDFLAGS += -L$(PCM_DIR)/lib/
        override LIBS += -Wl,-rpath=$(PCM_DIR)/lib -lpcm -lnuma -lm -pthread
        CFLAGS += -DARCH_INTEL
		SOURCES += src/IntelMonitor.cpp
    endif
    ifeq ($(VENDOR), AuthenticAMD)
        override LIBS += -lnuma -lm
        CFLAGS += -DARCH_AMD
		SOURCES += src/AMDMonitor.cpp
    endif
endif

SOURCES += src/Tierd.cpp

OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
