SMDK_ROOT_DIR := $(CURDIR)/../../..
KDIR := $(SMDK_ROOT_DIR)/lib/linux-6.4-smdk
obj-m += bwd.o

all:
	make -C $(KDIR) M=$(CURDIR) modules

clean:
	make -C $(KDIR) M=$(CURDIR) clean
