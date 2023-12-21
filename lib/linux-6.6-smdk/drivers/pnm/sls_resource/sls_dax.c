// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include "device_resource.h"
#include "mem_info.h"
#include "sls.h"

#include <linux/device.h>
#include <linux/memremap.h>
#include <linux/pnm/log.h>
#include <linux/sls_common.h>
#include <linux/sls_resources.h>

static struct dev_pagemap *
create_pnm_pagemap(struct device *dev, const struct sls_mem_info *mem_info)
{
	uint64_t base_addr;
	int nr_regions;
	struct dev_pagemap *pgmap;

	if (!dev)
		return NULL;

	if (!mem_info)
		return NULL;

	nr_regions = mem_info->nr_regions;
	base_addr = sls_get_base();

	pgmap = devm_kzalloc(dev, sizeof(struct dev_pagemap) * nr_regions,
			     GFP_KERNEL);
	if (!pgmap)
		return NULL;

	for (int i = 0; i < nr_regions; i++) {
		uint64_t start = mem_info->regions[i].range.start;
		uint64_t end = mem_info->regions[i].range.end;

		pgmap[i].type = sls_get_cache_policy(mem_info->regions[i].type);
		pgmap[i].range = (struct range){
			.start = (phys_addr_t)base_addr + start,
			.end = (phys_addr_t)base_addr + end,
		};
		pgmap[i].nr_range = 1;

		PNM_INF("DAX pagemap[%d] %llx : %llx ", i, pgmap[i].range.start,
			pgmap[i].range.end);
		if (pgmap[i].type == MEMORY_DEVICE_GENERIC_WCCACHE)
			PNM_INF("Write-Combine cache policy\n");
		else
			PNM_INF("Write-Back cache policy\n");
	}

	return pgmap;
}

int init_sls_dax_dev(struct device *dev, int region_id, int dax_id)
{
	struct dev_pagemap *pgmap;
	uint64_t total_size, base_addr;
	int nr_regions, target_node, err;
	struct pnm_region region;
	const struct sls_mem_info *mem_info;

	if (dev == NULL) {
		PNM_ERR("sls_resource is not initialized\n");
		return -EINVAL;
	}

	mem_info = sls_get_mem_info();
	if (mem_info == NULL) {
		PNM_ERR("sls_resource is not initialized\n");
		return -ENXIO;
	}

	nr_regions = mem_info->nr_regions;
	if (nr_regions == 0) {
		PNM_ERR("mem_info is not configured\n");
		return -ENXIO;
	}

	total_size = sls_get_memory_size();
	if (total_size == 0) {
		PNM_ERR("mem_info is not configured\n");
		return -ENXIO;
	}

	base_addr = sls_get_base();
	if (base_addr == 0) {
		PNM_ERR("mem_info is not configured\n");
		return -ENXIO;
	}

	target_node = phys_to_target_node(base_addr);
	if (target_node == NUMA_NO_NODE)
		target_node = memory_add_physaddr_to_nid(base_addr);

	// [TODO: @alex.antonov] dax_region leak if the code below is failed
	pgmap = create_pnm_pagemap(dev, mem_info);
	if (!pgmap) {
		PNM_ERR("can not create pagemap\n");
		return -ENXIO;
	}

	region = (struct pnm_region){
		.region_id = region_id,
		.dax_id = dax_id,
		.range.start = base_addr,
		.range.end = base_addr + total_size - 1,
		.align = sls_get_align(),
		.pgmap = pgmap,
		.nr_pgmap = nr_regions,
	};

	err = init_pnm_dax_dev(dev, &region);
	if (!err)
		PNM_INF("DAX Enabled\n");
	else
		PNM_ERR("Can not create DAX\n");
	return err;
}

/* [TODO: @alex.antonov] make it possible to release DAX device during sls deinitialization
 * void release_sls_dax_dev(struct device *dev)
 */
