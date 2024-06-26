/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __SLS_AXDIMM_PARAMETERS_H__
#define __SLS_AXDIMM_PARAMETERS_H__

#include "device_resource.h"
#include "params.h"

#include <linux/sizes.h>
#include <linux/sls_common.h>

static const struct sls_topology axdimm_default_sls_topology = {
	.dev_type = SLS_AXDIMM,
	.nr_ranks = 4,
	.nr_cunits = 4, // nr_ranks
	.nr_cs = 2,
	.nr_ch = 1,
	.aligned_tag_sz = SZ_64,
	.inst_sz = 8,
	.data_sz = 4,
	.buf_sz = SZ_256K,
	.nr_inst_buf = 1,
	.nr_psum_buf = 2,
	.nr_tag_buf = 2,
	.reg_en = 0x0,
	.reg_exec = 0x40,
	.reg_poll =
		0x440, // This offset is RA offset, system offset equal to 0x840.
	.alignment_sz = PAGE_SIZE,
	.mem_size_gb = SLS_AXDIMM_HW_MEM_SZ_GB,
	.block_sz = {},
	.base_addr = 66ULL << ONE_GB_SHIFT, // hardware base addr
};

#endif // __SLS_AXDIMM_PARAMETERS_H__
