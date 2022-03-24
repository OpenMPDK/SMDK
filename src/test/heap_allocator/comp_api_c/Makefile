SMDK_ROOT_DIR := $(CURDIR)/../../../..

TC1 := test_heap_alloc

all:  $(TC1)

$(TC1) : $(TC1).c
	$(QUIET_CC)$(CC) $< -o $@ 

clean:
	rm -f *.o $(TC1)
