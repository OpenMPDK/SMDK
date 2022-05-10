JEMALLOC_DIR:= jemalloc-5.2.1
OPT_DIR:= ./opt_api
COMP_DIR:= ./comp_api

all : SMDK_api jemalloc

jemalloc :
	cd $(JEMALLOC_DIR) && make -j

opt_api : jemalloc
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

clean_SMDK :
	cd $(OPT_DIR) && make clean
	cd $(COMP_DIR) && make clean

clean_lib :
	rm -rf lib

clean: clean_jemalloc clean_SMDK clean_lib

.PHONY: all clean jemalloc SMDK_api opt_api comp_api
