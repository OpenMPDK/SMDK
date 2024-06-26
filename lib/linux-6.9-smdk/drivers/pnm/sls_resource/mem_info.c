// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#include "mem_info.h"
#include "topo/params.h"

#include <linux/memremap.h>
#include <linux/module.h>
#include <linux/pnm/log.h>
#include <linux/slab.h>

static const struct sls_mem_info *mem_info;

#define NR_REGIONS_TOPO_EXPR(nr_regions_per_cunit)                      \
	((sls_topo()->dev_type == SLS_AXDIMM ? sls_topo()->nr_cs :      \
					       sls_topo()->nr_cunits) * \
	 (nr_regions_per_cunit))

const struct sls_mem_info *sls_create_mem_info(void)
{
	const size_t nr_regions = NR_REGIONS_TOPO_EXPR(SLS_BLOCK_MAX);
	// local mem_info is needed to avoid casting from "const"
	struct sls_mem_info *l_mem_info = NULL;
	size_t start = 0;
	enum sls_mem_blocks_e type;
	uint8_t idx;

	PNM_DBG("Building sls mem_info\n");

	l_mem_info = kzalloc(struct_size(l_mem_info, regions, nr_regions),
			     GFP_KERNEL);

	if (!l_mem_info)
		return NULL;

	l_mem_info->nr_regions = nr_regions;

	for (idx = 0; idx < nr_regions; ++idx) {
		type = idx % SLS_BLOCK_MAX;

		l_mem_info->regions[idx].range = (struct range){
			.start = start,
			.end = start + sls_topo()->block_sz[type] -
			       1, // struct range is closed
		};

		l_mem_info->regions[idx].type = type;
		start = l_mem_info->regions[idx].range.end +
			1; // struct range is closed
	}

	mem_info = l_mem_info;
	return mem_info;
}

const struct sls_mem_info *sls_get_mem_info(void)
{
	return mem_info;
}
EXPORT_SYMBOL(sls_get_mem_info);

void sls_destroy_mem_info(void)
{
	kfree(mem_info);
}
EXPORT_SYMBOL(sls_destroy_mem_info);

enum memory_type sls_get_cache_policy(enum sls_mem_blocks_e type)
{
	enum memory_type policy;

	switch (type) {
	case SLS_BLOCK_BASE:
	case SLS_BLOCK_CFGR:
	case SLS_BLOCK_INST:
		policy = MEMORY_DEVICE_GENERIC_WCCACHE;
		break;
	default:
		policy = MEMORY_DEVICE_GENERIC;
		break;
	}

	return policy;
}
EXPORT_SYMBOL(sls_get_cache_policy);

static inline void make_cunit_regions(uint64_t nr_regions_per_cunit,
				      int nr_interleaved_cu,
				      struct sls_mem_cunit_region *dst_regions,
				      const struct sls_mem_region *src_regions)
{
	const struct sls_mem_region *src = src_regions;
	struct sls_mem_cunit_region *dst = dst_regions;
	uint8_t r = 0;

	for (r = 0; r < nr_regions_per_cunit; ++r) {
		dst[r].type = src[r].type;
		dst[r].map_range = dst[r].range = src[r].range;
		dst[r].range.end =
			dst[r].range.start +
			range_len(&dst[r].map_range) / nr_interleaved_cu -
			1; // struct range is closed
	}
}

static void make_cunit_info(uint64_t nr_regions_per_cunit,
			    struct sls_mem_cunit_info *mem_cunit_info,
			    const struct sls_mem_info *mem_info)
{
	const struct sls_mem_region *src = NULL;
	struct sls_mem_cunit_region *dst = NULL;
	const int nr_cu = sls_topo()->nr_cunits;
	const int nr_interleaved_cu =
		sls_topo()->dev_type == SLS_AXDIMM ? NR_CUNITS_PER_CS : 1;
	int cu = 0, interleaved_cu = 0;

	/* Here we create compute units layout according to interleaving model */
	for (cu = 0; cu < nr_cu; ++cu) {
		interleaved_cu = cu / nr_interleaved_cu;

		/* choose device regions array */
		src = &mem_info->regions[interleaved_cu * nr_regions_per_cunit];

		/* choose cunit regions array */
		dst = &mem_cunit_info->regions[cu * nr_regions_per_cunit];

		/* form cunit address layout */
		make_cunit_regions(nr_regions_per_cunit, nr_interleaved_cu, dst,
				   src);
	}
}

const struct sls_mem_cunit_info *
sls_create_mem_cunit_info(const struct sls_mem_info *mem_info)
{
	const uint64_t nr_cunits = sls_topo()->nr_cunits;
	const uint64_t nr_regions_per_cunit = SLS_BLOCK_MAX;
	const uint64_t nr_regions = nr_regions_per_cunit * nr_cunits;
	struct sls_mem_cunit_info *mem_cunit_info = NULL;

	PNM_DBG("Building sls mem cunit info\n");

	mem_cunit_info = kzalloc(
		struct_size(mem_cunit_info, regions, nr_regions), GFP_KERNEL);

	if (!mem_cunit_info)
		return NULL;

	mem_cunit_info->nr_cunits = nr_cunits;
	mem_cunit_info->nr_regions = nr_regions;

	make_cunit_info(nr_regions_per_cunit, mem_cunit_info, mem_info);

	return mem_cunit_info;
}

void sls_destroy_mem_cunit_info(const struct sls_mem_cunit_info *cunit_mem_info)
{
	kfree(cunit_mem_info);
}
EXPORT_SYMBOL(sls_destroy_mem_cunit_info);

uint64_t sls_get_memory_size(void)
{
	const size_t nr_regions = NR_REGIONS_TOPO_EXPR(SLS_BLOCK_MAX);
	uint64_t size = 0;
	uint8_t idx;

	for (idx = 0; idx < nr_regions; ++idx)
		size += sls_topo()->block_sz[idx % SLS_BLOCK_MAX];

	return size;
}
EXPORT_SYMBOL(sls_get_memory_size);

uint64_t sls_get_base(void)
{
	return sls_topo()->base_addr;
}
EXPORT_SYMBOL(sls_get_base);

int sls_get_align(void)
{
	return sls_topo()->alignment_sz;
}
EXPORT_SYMBOL(sls_get_align);
