obj-$(CONFIG_SLS_RESOURCE) += sls_resource.o
obj-$(CONFIG_DEV_SLS_CXL_BUS_DRIVER) += slscxl.o

ccflags-y += -DPNM_LOG_COMPONENT_MARK=\"[SLS]\"
CFLAGS_sls.o += -DSLS_DRIVER_VERSION=\"$(shell git rev-parse --short HEAD)\"
subdir-ccflags-y += -I $(srctree)/$(src)/ -I $(srctree)/$(src)/.. -I $(srctree)/drivers/

sysfs-y := sysfs/cunits.o
sysfs-y += sysfs/mappings.o
sysfs-y += sysfs/sysfs.o
sysfs-y += sysfs/topology.o

topo-y := topo/params.o

sls_resource-y := allocator.o
sls_resource-y += cunit_sched.o
sls_resource-y += mem_info.o
sls_resource-y += process_manager.o
sls_resource-y += sls.o
sls_resource-y += $(sysfs-y)
sls_resource-y += resource.o
sls_resource-y += $(topo-y)
sls_resource-$(CONFIG_DEV_DAX) += sls_dax.o

slscxl-y += sls_cxl.o
slscxl-y += sls_dax.o
