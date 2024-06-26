// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include "params.h"
#include "axdimm.h"
#include "cxl.h"
#include "device_resource.h"

#include <linux/bitfield.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pnm/log.h>
#include <linux/sls_common.h>

// [TODO:] Get rid of kconfig dev_type parameter
#if CONFIG_DEV_SLS_BUS == DEV_TYPE_SLS_AXDIMM
static struct sls_params sls_params = {
	.dev_type = { .value = SLS_AXDIMM, .is_set = true },
};
#elif CONFIG_DEV_SLS_BUS == DEV_TYPE_SLS_CXL
static struct sls_params sls_params = {
	.dev_type = { .value = SLS_CXL, .is_set = true },
};
#else /* CONFIG_DEV_SLS_BUS == DEV_TYPE_SLS_CXL */
#error "SLS bus/interface should be selected!"
#endif /* CONFIG_DEV_SLS_BUS == DEV_TYPE_SLS_AXDIMM */

static struct sls_topology sls_topology;

#define APPLY_PARAMETER(param)                                       \
	{                                                            \
		if (sls_params.param.is_set) {                       \
			sls_topology.param = sls_params.param.value; \
		} else {                                             \
			sls_params.param.value = sls_topology.param; \
		}                                                    \
	}

#define CHECK_PARAMETER(result, param, expr)                     \
	{                                                        \
		if (sls_params.param.is_set) {                   \
			(result) &= sls_params.param.value expr; \
		}                                                \
	}

/* CS offsets for interleaved ranks */
// bankgroup 0 bank 0 row 0 column 0
#define BASE_OFFSET 0x000000000
// bankgroup 0 bank 0 row 0x1F000 column 0
#define INST_OFFSET 0x7c0000000
// bankgroup 0 bank 0 row 0x1F100 column 0
#define CFGR_OFFSET 0x7c2000000
// bankgroup 0 bank 0 row 0x1F200 column 0
#define TAGS_OFFSET 0x7c4000000
// bankgroup 0 bank 0 row 0x1F400 column 0
#define PSUM_OFFSET 0x7c8000000
#define CS1_OFFSET 0x800000000

/* Mask for scaling */
#define SCALE_MASK GENMASK_ULL(36, 30)

/*
 * Here we do right shift only for [30:36] bits,
 * CH and CS from 36 and 35 position
 * and five row bits from [30:34] position
 */
static uint64_t sls_apply_scale(uint64_t addr, int scale)
{
	const uint64_t scaled_bits = FIELD_GET(SCALE_MASK, addr) >> scale;
	const uint64_t cleared_addr = addr & (~SCALE_MASK);

	return cleared_addr | FIELD_PREP(SCALE_MASK, scaled_bits);
}

static void set_mem_blocks_sizes(u64 *block_sz, int scale)
{
	size_t offsets[SLS_BLOCK_MAX] = {};
	int block;

	offsets[SLS_BLOCK_BASE] = sls_apply_scale(BASE_OFFSET, scale);
	offsets[SLS_BLOCK_INST] = sls_apply_scale(INST_OFFSET, scale);
	offsets[SLS_BLOCK_CFGR] = sls_apply_scale(CFGR_OFFSET, scale);
	offsets[SLS_BLOCK_TAGS] = sls_apply_scale(TAGS_OFFSET, scale);
	offsets[SLS_BLOCK_PSUM] = sls_apply_scale(PSUM_OFFSET, scale);

	/* init register size for each cs */
	block_sz[SLS_BLOCK_BASE] =
		offsets[SLS_BLOCK_INST] - offsets[SLS_BLOCK_BASE];
	block_sz[SLS_BLOCK_INST] =
		offsets[SLS_BLOCK_CFGR] - offsets[SLS_BLOCK_INST];
	block_sz[SLS_BLOCK_CFGR] =
		offsets[SLS_BLOCK_TAGS] - offsets[SLS_BLOCK_CFGR];
	block_sz[SLS_BLOCK_TAGS] =
		offsets[SLS_BLOCK_PSUM] - offsets[SLS_BLOCK_TAGS];
	block_sz[SLS_BLOCK_PSUM] =
		sls_apply_scale(CS1_OFFSET, scale) - offsets[SLS_BLOCK_PSUM];

	if (sls_topo()->dev_type == SLS_AXDIMM)
		return;

	/* one rank per CS, CXL type, need to adjust offsets and size */
	for (block = 0; block < SLS_BLOCK_MAX; ++block) {
		/* [TODO: MCS23-1373] get rid of magic '2' once mem_info
		 * initialization will be refactored by above task
		 */
		block_sz[block] /= 2;
	}
}

int init_topology(void)
{
	int scale, default_mem_size_gb;

	APPLY_PARAMETER(dev_type);

	switch ((enum sls_device_type)sls_topology.dev_type) {
	case SLS_AXDIMM:
		sls_topology = axdimm_default_sls_topology;
		break;
	case SLS_CXL:
		sls_topology = cxl_default_sls_topology;
		break;
	case SLS_UNDEFINED:
	case SLS_MAX:
		return -EINVAL;
	}

	// to calculate "scale" we need to know original hw mem_size_gb that
	// is placed in sls_topology.mem_size_gb. Then sls_topology.mem_size_gb
	// is rewrote in APPLY_PARAMETER if mem_size_gb is set by the parameter.
	default_mem_size_gb = sls_topology.mem_size_gb;

	APPLY_PARAMETER(mem_size_gb);
	APPLY_PARAMETER(nr_cunits);
	APPLY_PARAMETER(aligned_tag_sz);
	APPLY_PARAMETER(data_sz);
	APPLY_PARAMETER(buf_sz);
	APPLY_PARAMETER(nr_inst_buf);
	APPLY_PARAMETER(nr_psum_buf);
	APPLY_PARAMETER(nr_tag_buf);
	APPLY_PARAMETER(nr_cunits);
	APPLY_PARAMETER(base_addr);

	APPLY_PARAMETER(reg_en);
	APPLY_PARAMETER(reg_exec);
	APPLY_PARAMETER(reg_poll);

	/* setup topology according to memory model */

	/* cxl and axdimm both have nr_cunits equal to nr_ranks */
	sls_topology.nr_ranks = sls_topology.nr_cunits;
	/* deduce number of CS from ranks */
	sls_topology.nr_cs = roundup(sls_topology.nr_ranks, NR_CUNITS_PER_CS) /
			     NR_CUNITS_PER_CS;
	/* deduce of CH from CS */
	sls_topology.nr_ch =
		roundup(sls_topology.nr_cs, NR_CS_PER_CH) / NR_CS_PER_CH;

	scale = ilog2(default_mem_size_gb / sls_topology.mem_size_gb);
	set_mem_blocks_sizes(sls_topology.block_sz, scale);

	return 0;
}

const struct sls_topology *sls_topo(void)
{
	return &sls_topology;
}

// Check that topology in `params` is consistent.
static int validate_parameters(void)
{
	bool is_valid = true;
	const size_t buf_alignment = 64;
	// A minimal memory size is defined by size of control blocks
	// (CFGR, INSTR, TAGS, PSUMS) that do not scale and have overall
	// size 512MB per single compute unit.
	const int64_t min_memory_size =
		roundup(sls_params.nr_cunits.value, 2) / 2;
	const int64_t max_memory_size = sls_params.dev_type.value ==
							SLS_AXDIMM ?
						SLS_AXDIMM_HW_MEM_SZ_GB :
						SLS_CXL_HW_MEM_SZ_GB;

	CHECK_PARAMETER(is_valid, buf_sz, % buf_alignment == 0);
	CHECK_PARAMETER(is_valid, nr_cunits, != 0);
	CHECK_PARAMETER(is_valid, nr_cunits, % 2 == 0);
	CHECK_PARAMETER(is_valid, aligned_tag_sz, == SZ_64);
	CHECK_PARAMETER(is_valid, mem_size_gb, >= min_memory_size);
	CHECK_PARAMETER(is_valid, mem_size_gb, <= max_memory_size);

	if (sls_params.mem_size_gb.is_set)
		is_valid &= is_power_of_2(sls_params.mem_size_gb.value);

	return is_valid ? 0 : -EINVAL;
}

static int set_dev_type(const char *val, const struct kernel_param *kp)
{
	struct sls_param *const param =
		container_of((int64_t *)kp->arg, struct sls_param, value);
	int rc = -EINVAL;

	if (strcmp("AXDIMM", val) == 0) {
		param->value = SLS_AXDIMM;
		rc = 0;
	} else if (strcmp("CXL", val) == 0) {
		param->value = SLS_CXL;
		rc = 0;
	}

	if (rc) {
		PNM_ERR("Unknown device name: %s\n", val);
		return rc;
	}

	param->is_set = true;

	return rc;
}

static int get_dev_type(char *buffer, const struct kernel_param *kp)
{
	const struct sls_param *const param =
		container_of((int64_t *)kp->arg, struct sls_param, value);

	switch ((const enum sls_device_type)param->value) {
	case SLS_AXDIMM:
		return sysfs_emit(buffer, "AXDIMM\n");
	case SLS_CXL:
		return sysfs_emit(buffer, "CXL\n");
	case SLS_UNDEFINED:
	case SLS_MAX:
		break;
	}

	PNM_ERR("Unknown device name: %lld\n", param->value);

	return -EINVAL;
}

static const struct kernel_param_ops param_ops_dev_type = {
	.set = set_dev_type,
	.get = get_dev_type,
};

static int set_base_addr(const char *val, const struct kernel_param *kp)
{
	struct sls_param *param;
	uint64_t value;
	const int rc = kstrtoll(val, 0, &value);

	if (rc)
		return rc;

	if (value < 0)
		return rc;

	param = container_of(((int64_t *)kp->arg), struct sls_param, value);
	param->value = value << ONE_GB_SHIFT;
	param->is_set = true;

	return 0;
}

static int get_base_addr(char *buffer, const struct kernel_param *kp)
{
	const struct sls_param *param =
		container_of((int64_t *)kp->arg, struct sls_param, value);
	const uint64_t value = param->value >> ONE_GB_SHIFT;

	return sysfs_emit(buffer, "%llu\n", value);
}

static const struct kernel_param_ops param_ops_base_addr = {
	.set = set_base_addr,
	.get = get_base_addr,
};

static int set_value(const char *value, const struct kernel_param *kp)
{
	struct sls_param *param =
		container_of((int64_t *)kp->arg, struct sls_param, value);
	int rc;

	param->is_set = true;
	rc = kstrtoll(value, 0, &param->value);
	if (rc != 0)
		return rc;

	// Common rule: parameter values should not be negative.
	if (param->value < 0)
		return -EINVAL;

	rc = validate_parameters();
	if (rc != 0)
		return rc;

	return rc;
}

static int get_value(char *buffer, const struct kernel_param *kp)
{
	return sysfs_emit(buffer, "%d\n", *((int *)kp->arg));
}

static const struct kernel_param_ops param_ops_topo = {
	.set = set_value,
	.get = get_value,
};

#define param_check_dev_type(name, p) __param_check(name, p, int64_t)
#define param_check_topo(name, p) __param_check(name, p, int64_t)
#define param_check_base_addr(name, p) __param_check(name, p, int64_t)

#define TYPED_NAMED_PARAM(param, type) \
	module_param_named(param, sls_params.param.value, type, 0444)

#define NAMED_PARAM(param) TYPED_NAMED_PARAM(param, topo)

TYPED_NAMED_PARAM(dev_type, dev_type);
TYPED_NAMED_PARAM(base_addr, base_addr);
NAMED_PARAM(nr_cunits);
NAMED_PARAM(aligned_tag_sz);
NAMED_PARAM(data_sz);
NAMED_PARAM(buf_sz);
NAMED_PARAM(nr_inst_buf);
NAMED_PARAM(nr_psum_buf);
NAMED_PARAM(nr_tag_buf);
NAMED_PARAM(mem_size_gb);
