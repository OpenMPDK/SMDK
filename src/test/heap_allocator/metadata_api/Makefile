CC=gcc
CFLAGS=-W -Wall

SMDK_ROOT_DIR := $(CURDIR)/../../../..
CXLMALLOC_DIR=$(SMDK_ROOT_DIR)/lib/smdk_allocator
override CFLAGS += -I$(CXLMALLOC_DIR)/opt_api/include -I$(SMDK_ROOT_DIR)/src/test/include
override LDFLAGS += -L$(CXLMALLOC_DIR)/lib/
override LIBS += -lc -lpthread -lsmalloc #for dynamic link
#override LIBS += $(CXLMALLOC_DIR)/lib/libsmalloc.a -lpthread -ldl #for static link

TC1=test_metadata_api
TC2=test_metadata_api_node

all: $(TC1) $(TC2)

$(TC1): $(TC1).o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

$(TC2): $(TC2).o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -f *.o $(TC1) $(TC2)
