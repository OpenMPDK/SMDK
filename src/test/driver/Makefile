SMDK_ROOT_DIR := $(CURDIR)/../../../

CC=gcc
CFLAGS=-W -Wall	-I$(SMDK_ROOT_DIR)/src/test/include
APP=mmap_cxl
OBJS=mmap_cxl.o

all: $(APP)

$(APP): $(OBJS) 
	$(CC) $(CFLAGS) -o $@ $^ -lnuma

clean:
	rm -rf *.o $(APP)
