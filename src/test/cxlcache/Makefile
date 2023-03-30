SMDK_ROOT_DIR := $(CURDIR)/../../..
SUBDIRS=
cc=gcc
CFALGS=-W -Wall -I$(SMDK_ROOT_DIR)/src/test/include 
APP=test_cxlcache
OBJS=test_cxlcache.o

.PHONY:subdirs $(SUBDIRS)

all: $(APP)
	for DIRS in $(SUBDIRS); do\
		$(MAKE) -C $$DIRS;\
	done

$(APP): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lnuma

clean:
	rm -rf *.o $(APP)
	for DIRS in $(SUBDIRS); do\
		$(MAKE) -C $$DIRS clean;\
	done
