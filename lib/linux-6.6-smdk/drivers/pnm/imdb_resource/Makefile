# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_IMDB_RESOURCE) += imdb_resource.o
obj-$(CONFIG_CXL_IMDB) += imdbcxl.o

ccflags-y += -DPNM_LOG_COMPONENT_MARK=\"[IMDB]\"
subdir-ccflags-y += -I $(srctree)/$(src)/ -I $(srctree)/$(src)/.. -I $(srctree)/drivers/

topo-y := topo/export.o
topo-y += topo/params.o

imdb_resource-y := resource_device.o
imdb_resource-y += allocator.o
imdb_resource-y += thread_sched.o
imdb_resource-y += sysfs.o
imdb_resource-y += proc_mgr.o
imdb_resource-y += $(topo-y)
imdb_resource-$(CONFIG_DEV_DAX) += imdb_dax.o

imdbcxl-y += imdb_cxl.o
imdbcxl-y += imdb_dax.o
