CC := gcc
LIBSMALLOC = ../lib/libsmalloc.so
LIBSMALLOC_STATIC = ../lib/libsmalloc.a
C_SRC := smdk_opt_api.c \
	 opt_api.c \
	 config.c
C_OBJS := $(C_SRC:%.c=%.o)

SMDK_ROOT_DIR := $(CURDIR)/..
JEMALLOC_DIR=$(SMDK_ROOT_DIR)/jemalloc-5.2.1
JEMALLOC_SLIB_PIC=$(JEMALLOC_DIR)/lib/libjemalloc_pic.a
JEMALLOC_SLIB=$(JEMALLOC_DIR)/lib/libjemalloc.a

override CFLAGS += -I$(JEMALLOC_DIR)/include \
		   -I./include \
		   -D_GNU_SOURCE -fPIC
override LDFLAGS += -L$(JEMALLOC_DIR)/lib/
override LIBS +=-Wl,--whole-archive $(JEMALLOC_SLIB_PIC) -Wl,--no-whole-archive -lpthread -ldl

all: $(LIBSMALLOC) static_lib
$(LIBSMALLOC): $(C_OBJS)
	$(CC) -Wall -shared -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

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
	rm -f *.o $(LIBSMALLOC) $(LIBSMALLOC_STATIC)
