/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

/*
 * This file contains common definitions and macros used both by DAX and
 * sls_resource.
 */

#ifndef __SLS_COMMON_H__
#define __SLS_COMMON_H__

#include <linux/sls_resources.h>
#include <linux/io.h>
#include <linux/range.h>
#include <linux/types.h>

/* [TODO: @p.bred] Get rid of whole file, export necessary for DAX API */

#define DEV_TYPE_SLS_AXDIMM 1
#define SLS_AXDIMM_HW_MEM_SZ_GB 64

#define DEV_TYPE_SLS_CXL 2
#define SLS_CXL_HW_MEM_SZ_GB 32

enum sls_device_type {
	SLS_UNDEFINED = 0,
	SLS_AXDIMM = DEV_TYPE_SLS_AXDIMM,
	SLS_CXL = DEV_TYPE_SLS_CXL,
	SLS_MAX,
};

struct sls_mem_region {
	enum sls_mem_blocks_e type;
	struct range range;
};

struct sls_mem_info {
	uint64_t nr_regions;
	struct sls_mem_region regions[];
};

struct sls_mem_cunit_region {
	enum sls_mem_blocks_e type;
	struct range range;
	struct range map_range;
};

struct sls_mem_cunit_info {
	uint64_t nr_cunits;
	uint64_t nr_regions;
	struct sls_mem_cunit_region regions[];
};

#endif /* __SLS_COMMON_H__ */
