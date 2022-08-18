SMDK_ROOT_DIR := $(CURDIR)/../../..
CC=gcc
CFLAGS=-W -Wall -I$(SMDK_ROOT_DIR)/src/test/include
APP=test_mmap_cxl
OBJS=test_mmap_cxl.o

all: $(APP)

$(APP): $(OBJS) 
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf *.o $(APP)
