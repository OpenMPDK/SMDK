CXX=g++
CXXFLAGS=-W -Wall

SMDK_ROOT_DIR := $(CURDIR)/../../../..
CXLMALLOC_DIR=$(SMDK_ROOT_DIR)/lib/smdk_allocator
PNMLIB_DIR=$(SMDK_ROOT_DIR)/lib/PNMLibrary-pnm-v3.0.0/PNMLibrary
override CXXFLAGS += -I$(CXLMALLOC_DIR)/opt_api/include -I$(SMDK_ROOT_DIR)/src/test/include -I$(PNMLIB_DIR)/build/libs/include
override LDFLAGS += -L$(CXLMALLOC_DIR)/lib/
override LIBS += -lc -lnuma -lpthread -lsmalloc -lpnm #for dynamic link
#override LIBS += $(CXLMALLOC_DIR)/lib/libsmalloc.a -lpthread -ldl #for static link

TC1=test_opt_api_cpp

all: $(TC1)

$(TC1): $(TC1).o
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -f *.o $(TC1)
