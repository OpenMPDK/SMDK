SMDK_ROOT_DIR := $(CURDIR)/../../..
CC=gcc
CFLAGS=-W -Wall -I$(SMDK_ROOT_DIR)/src/test/include
APP=devmem2
OBJS=devmem2.o

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf *.o $(APP)
