// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include "dax/bus.h"

#include "device_resource.h"
#include "private.h"

#include <linux/pnm/log.h>

int init_imdb_dax_dev(struct device *dev, int region_id)
{
	int err;
	struct pnm_region region;
	uint64_t size;
	phys_addr_t base_addr;

	if (dev == NULL) {
		PNM_ERR("imdb_resource is not initialized\n");
		return -EINVAL;
	}

	base_addr = imdb_get_data_addr();
	if (base_addr == 0) {
		PNM_ERR("imdb_resource is not initialized\n");
		return -ENODEV;
	}
	size = imdb_get_mem_size();
	if (size == 0) {
		PNM_ERR("imdb_resource is not initialized\n");
		return -ENODEV;
	}

	region = (struct pnm_region){
		.region_id = region_id, // dax0.0
		.range.start = base_addr,
		.range.end = base_addr + size - 1,
		.pgmap = NULL,
		.nr_pgmap = 0,
		// if pagemap is not set, dax_id must be "-1" - dax logic
		.dax_id = -1,
		.align = IMDB_MEMORY_ADDRESS_ALIGN,
	};

	PNM_INF("IMDB dax range: %llx : %llx\n", region.range.start,
		region.range.end);
	err = init_pnm_dax_dev(dev, &region);
	if (!err)
		PNM_INF("DAX Enabled\n");
	else
		PNM_ERR("Can not create DAX\n");
	return err;
}

/* [TODO: @alex.antonov] make it possible to release DAX device during imdb deinitialization
 * void release_imdb_dax_dev(struct device *dev)
 */
