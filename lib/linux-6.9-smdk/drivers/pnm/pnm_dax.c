// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include "asm-generic/errno-base.h"
#include "dax/bus.h"

#include "device_resource.h"

#include <linux/dax.h>
#include <linux/device.h>
#include <linux/memremap.h>
#include <linux/pnm/log.h>
#include <linux/range.h>

int init_pnm_dax_dev(struct device *dev, struct pnm_region *region)
{
	struct dev_dax_data data;
	struct dax_region *dax_region;
	uint64_t flags;
	int target_node;
	int err;

	target_node = phys_to_target_node(region->range.start);
	if (target_node == NUMA_NO_NODE)
		target_node = memory_add_physaddr_to_nid(region->range.start);

	// If we need the special pgmap, DAX subsystem handles this as IORESOURCE_DAX_STATIC
	flags = (region->pgmap != NULL) ? IORESOURCE_DAX_STATIC : 0;
	dax_region = alloc_dax_region(dev, region->region_id, &region->range,
				      target_node, region->align, flags);
	if (dax_region == NULL) {
		PNM_ERR("Can not allocate dax_region\n");
		return -EINVAL;
	}

	data = (struct dev_dax_data){
		.dax_region = dax_region,
		.id = region->dax_id,
		.size = region->range.end - region->range.start + 1,
		.pgmap = region->pgmap,
		.nr_pgmap = region->nr_pgmap,
	};

	err = PTR_ERR_OR_ZERO(devm_create_dev_dax(&data));
	if (!err)
		PNM_INF("DAX Enabled\n");
	else
		PNM_ERR("Can not create DAX\n");
	return err;
}
EXPORT_SYMBOL(init_pnm_dax_dev);
