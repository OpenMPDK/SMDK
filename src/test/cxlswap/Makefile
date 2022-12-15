SMDK_ROOT_DIR := $(CURDIR)/../../..
cc=gcc
CFLAGS=-W -Wall -fopenmp -O2 -I$(SMDK_ROOT_DIR)/src/test/include
APP=test_cxlswap
OBJS=test_cxlswap.o

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf *.o $(APP)
