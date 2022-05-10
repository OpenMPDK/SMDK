CC=gcc
CFLAGS=-W -Wall

SMDK_ROOT_DIR := $(CURDIR)/../../../..

override LIBS += -lc -lpthread

TC1 := test_heap_alloc
TC2 := test_multi_thread

all:  $(TC1) $(TC2)

$(TC1) : $(TC1).c
	$(QUIET_CC)$(CC) $< -o $@ 

$(TC2) : $(TC2).c
	$(QUIET_CC)$(CC) $< -o $@  $(LIBS)

clean:
	rm -f *.o $(TC1) $(TC2)
