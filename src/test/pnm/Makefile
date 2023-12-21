CXX=g++
CXXFLAGS=-W -Wall -DSPDLOG_FMT_EXTERNAL -DPATH_TO_TABLES=\"$(CURDIR)/test_data/\"

SMDK_ROOT_DIR := $(CURDIR)/../../..
CXLMALLOC_DIR=$(SMDK_ROOT_DIR)/lib/smdk_allocator
PNMLIB_DIR=$(SMDK_ROOT_DIR)/lib/PNMLibrary-pnm-v3.0.0/PNMLibrary

override CXXFLAGS += -I$(PNMLIB_DIR) -I$(PNMLIB_DIR)/build/libs/include -I$(PNMLIB_DIR)/build/_deps/spdlog-src/include -I$(CXLMALLOC_DIR)/opt_api/include
override LDFLAGS += -L$(CXLMALLOC_DIR)/lib
override LIBS += $(PNMLIB_DIR)/build/_deps/fmt-build/libfmt.a $(PNMLIB_DIR)/build/_deps/spdlog-build/libspdlog.a $(PNMLIB_DIR)/build/tools/libimdb_datagen.a $(PNMLIB_DIR)/build/imdb/software_scan/libimdb_scan_avx2.a $(PNMLIB_DIR)/build/tools/libsls_datagen.a -lpnm -lsmalloc -lpthread

TC1=test_pnm_imdb
TC2=test_pnm_dlrm

all: $(TC1) $(TC2)

$(TC1): $(TC1).o
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS)
	mkdir -p $(CURDIR)/test_data

$(TC2): $(TC2).o
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS)
	mkdir -p $(CURDIR)/test_data

clean:
	rm -f *.o $(TC1)
	rm -f *.o $(TC2)
	rm -rf $(CURDIR)/test_data
