/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#ifndef __DEVICE_RESOURCE_H__
#define __DEVICE_RESOURCE_H__

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

// dax0.0
#define PNM_DAX_MINOR_ID 0
#define PNM_DAX_MAJOR_ID 0

#define ONE_GB_SHIFT 30

/**
 * struct pnm_region - pnm memory region descriptor to pass this in DAX
 * @region_id: minor number of daxN.N
 * @dax_id: major number of daxN.N
 * @range: memory range addresses
 * @align: allocation and mapping alignment for dax device
 * @pgmap: pgmap for memmap setup inside dax
 * @nr_range: size of @pgmap
 */
struct pnm_region {
	int region_id;
	int dax_id;
	struct range range;
	uint32_t align;
	struct dev_pagemap *pgmap;
	int nr_pgmap;
};

int pnm_create_resource_device(char *resource_device_name, int *device_number,
			       struct device **resource_device);

void pnm_destroy_resource_device(struct device *resource_device);

int init_pnm_dax_dev(struct device *dev, struct pnm_region *region);

#endif /* __DEVICE_RESOURCE_H__ */
