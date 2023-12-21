CC := gcc
CXX := g++

LIBSMALLOC = ../lib/libsmalloc.so
LIBSMALLOC_STATIC = ../lib/libsmalloc.a

SMDK_ROOT_DIR := $(CURDIR)/..
JEMALLOC_DIR=$(SMDK_ROOT_DIR)/jemalloc-5.2.1
JEMALLOC_SLIB_PIC=$(JEMALLOC_DIR)/lib/libjemalloc_pic.a
CORE_DIR=$(SMDK_ROOT_DIR)/core
PNMLIB_DIR=$(SMDK_ROOT_DIR)/../PNMLibrary-pnm-v3.0.0/PNMLibrary

C_SRC := smdk_opt_api.c \
	 smdk_opt_api_pnm.cpp \
	 smalloc.c \
	 meta_api.c \
	 $(CORE_DIR)/init.c \
	 $(CORE_DIR)/alloc.c \
	 $(CORE_DIR)/config.c

C_OBJS := $(C_SRC:%.c=%.o)
C_OBJS := $(C_OBJS:%.cpp=%.o)


override CFLAGS += -I$(JEMALLOC_DIR)/include \
		   -I./include \
		   -I$(CORE_DIR)/include \
		   -I$(SMDK_ROOT_DIR) \
		   -D_GNU_SOURCE -fPIC \
		   -I$(PNMLIB_DIR)/build/libs/include
override LDFLAGS += -L$(JEMALLOC_DIR)/lib/ -L$(PNMLIB_DIR)/build/libs/lib/
override LIBS +=-Wl,--whole-archive $(JEMALLOC_SLIB_PIC) -Wl,--no-whole-archive -lpthread -ldl -lnuma -lstdc++ -lpnm

all: $(LIBSMALLOC) static_lib
$(LIBSMALLOC): $(C_OBJS)
	$(CXX) -Wall -shared -o $@ $^ $(LDFLAGS) $(LIBS)
.c.o:
	$(CC) -Wall -shared -o $@ $< -c $(CFLAGS)
.cpp.o:
	$(CXX) -Wall -shared -o $@ $< -c $(CFLAGS)


define libadd
create $(LIBSMALLOC_STATIC)\n\
addlib $(LIBSMALLOC_STATIC)\n\
addlib $(JEMALLOC_SLIB_PIC)\n\
save\n\
end
endef

static_lib: $(LIBSMALLOC_STATIC)
	bash -c "ar -M < <(echo -e '$(libadd)')"
$(LIBSMALLOC_STATIC):$(C_OBJS)
	ar rscv $@ $^

clean:
	rm -f *.o $(CORE_DIR)/*.o $(LIBSMALLOC) $(LIBSMALLOC_STATIC)
