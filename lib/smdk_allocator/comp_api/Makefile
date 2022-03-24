CC := gcc
LIBCXLMALLOC = ../lib/libcxlmalloc.so
LIBCXLMALLOC_STATIC = ../lib/libcxlmalloc.a

SMDK_ROOT_DIR := $(CURDIR)/..
JEMALLOC_DIR=$(SMDK_ROOT_DIR)/jemalloc-5.2.1
JEMALLOC_SLIB_PIC=$(JEMALLOC_DIR)/lib/libjemalloc_pic.a
JEMALLOC_SLIB=$(JEMALLOC_DIR)/lib/libjemalloc.a
SMALLOC_DIR=$(SMDK_ROOT_DIR)/opt_api

C_SRC := smdk_comp_api.c \
	 comp_api.c \
	 cxlmalloc_cpp.cpp \
	 $(SMALLOC_DIR)/opt_api.c \
	 $(SMALLOC_DIR)/config.c

C_OBJS := $(C_SRC:%.c=%.o)

override CFLAGS += -I$(JEMALLOC_DIR)/include \
		   -I./include \
		   -I$(SMDK_ROOT_DIR) \
		   -I$(SMALLOC_DIR) \
		   -D_GNU_SOURCE -fPIC
override LDFLAGS += -L$(JEMALLOC_DIR)/lib/
override LIBS +=-Wl,--whole-archive $(JEMALLOC_SLIB_PIC) -Wl,--no-whole-archive -lpthread -ldl -lstdc++

all: $(LIBCXLMALLOC) static_lib
$(LIBCXLMALLOC): $(C_OBJS)
	$(CC) -Wall -shared -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

define libadd
create $(LIBCXLMALLOC_STATIC)\n\
addlib $(LIBCXLMALLOC_STATIC)\n\
addlib $(JEMALLOC_SLIB_PIC)\n\
save\n\
end
endef

static_lib: $(LIBCXLMALLOC_STATIC)
	bash -c "ar -M < <(echo -e '$(libadd)')"
$(LIBCXLMALLOC_STATIC):$(C_OBJS)
	ar rscv $@ $^

clean:
	rm -f *.o $(LIBCXLMALLOC) $(LIBCXLMALLOC_STATIC)
