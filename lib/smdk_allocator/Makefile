JEMALLOC_DIR:= jemalloc-5.2.1
OPT_DIR:= ./opt_api
COMP_DIR:= ./comp_api

all : SMDK_api jemalloc

jemalloc :
	cd $(JEMALLOC_DIR) && make -j

libpnm :
	mkdir -p lib
	cp $(CURDIR)/../PNMLibrary-pnm-v3.0.0/PNMLibrary/build/libs/lib/libpnm.so $(CURDIR)/lib/

opt_api : jemalloc libpnm
	mkdir -p lib
	cd $(OPT_DIR) && make

comp_api : jemalloc
	mkdir -p lib
	cd $(COMP_DIR) && make

SMDK_api : comp_api opt_api

clean_jemalloc :
ifneq (, $(wildcard $(JEMALLOC_DIR)/Makefile))
	cd $(JEMALLOC_DIR) && make distclean
endif

clean_pnm :
	rm -f $(CURDIR)/lib/libpnm.so

clean_SMDK :
	cd $(OPT_DIR) && make clean
	cd $(COMP_DIR) && make clean

clean_lib :
	rm -rf lib

clean: clean_jemalloc clean_SMDK clean_pnm clean_lib

.PHONY: all clean jemalloc SMDK_api opt_api comp_api
