PCM_DIR=pcm/build/

CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude

TARGET = tierd
SOURCES = src/monitor.c \
		  src/threshold.c \
		  src/policy.c \
		  src/distance.c \
		  src/util.c \
		  src/tierd.c
OBJECTS = $(SOURCES:.c=.o)

ARCH := $(shell uname -m)
ifeq ($(ARCH), x86_64)
    VENDOR := $(shell lscpu | grep 'Vendor ID' | awk '{print $$NF}' | head -n 1)
    ifeq ($(VENDOR), GenuineIntel)
        override LDFLAGS += -L$(PCM_DIR)/lib/
        override LIBS += -Wl,-rpath=$(PCM_DIR)/lib -lpcm -lnuma
        CFLAGS += -DARCH_INTEL
    endif
    ifeq ($(VENDOR), AuthenticAMD)
        override LIBS += -lnuma
        CFLAGS += -DARCH_AMD
    endif
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

