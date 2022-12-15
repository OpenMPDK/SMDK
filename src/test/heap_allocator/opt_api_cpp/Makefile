CC=g++
CXXFLAGS=-W -Wall

SMDK_ROOT_DIR := $(CURDIR)/../../../..
CXLMALLOC_DIR=$(SMDK_ROOT_DIR)/lib/smdk_allocator
override CXXFLAGS += -I$(CXLMALLOC_DIR)/opt_api/include -I$(SMDK_ROOT_DIR)/src/test/include
override LDFLAGS += -L$(CXLMALLOC_DIR)/lib/
override LIBS += -lc -lnuma -lpthread -lsmalloc #for dynamic link
#override LIBS += $(CXLMALLOC_DIR)/lib/libsmalloc.a -lpthread -ldl #for static link

TC1=test_opt_api_cpp

all: $(TC1)

$(TC1): $(TC1).o
	$(CC) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -f *.o $(TC1)
