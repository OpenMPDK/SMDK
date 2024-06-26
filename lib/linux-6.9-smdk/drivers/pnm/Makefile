# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_PNM) += pnm_class_resource.o

ccflags-y += -DPNM_LOG_COMPONENT_MARK=\"[RESOURCE_MGR]\"
subdir-ccflags-y += -I $(srctree)/drivers/

pnm_class_resource-y := class_resource.o
pnm_class_resource-y += pnm_dax.o

obj-$(CONFIG_SLS_RESOURCE) += sls_resource/
obj-$(CONFIG_IMDB_RESOURCE) += imdb_resource/
