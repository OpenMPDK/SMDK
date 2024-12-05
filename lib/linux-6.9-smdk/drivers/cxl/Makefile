# SPDX-License-Identifier: GPL-2.0
obj-y += core/
obj-$(CONFIG_CXL_PCI) += cxl_pci.o
obj-$(CONFIG_CXL_MEM) += cxl_mem.o
obj-$(CONFIG_CXL_HMU) += cxl_hmu.o
obj-$(CONFIG_CXL_ACPI) += cxl_acpi.o
obj-$(CONFIG_CXL_PMEM) += cxl_pmem.o
obj-$(CONFIG_CXL_PORT) += cxl_port.o

cxl_hmu-y := hmu.o
cxl_mem-y := mem.o
cxl_pci-y := pci.o
cxl_acpi-y := acpi.o
cxl_pmem-y := pmem.o security.o
cxl_port-y := port.o
