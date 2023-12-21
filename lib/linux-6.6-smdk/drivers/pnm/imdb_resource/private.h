/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __IMDB_PRIVATE_H__
#define __IMDB_PRIVATE_H__

#include <linux/imdb_resources.h>
#include <linux/pgtable.h>

/*
 * The default alignment for IMDB device in DAX is PMD_SIZE as hmem,
 * commonly it's 2MB
 */
#define IMDB_MEMORY_ADDRESS_ALIGN PMD_SIZE

uint64_t imdb_get_mem_size(void);
phys_addr_t imdb_get_data_addr(void);

#if IS_ENABLED(CONFIG_DEV_DAX)
int init_imdb_dax_dev(struct device *dev, int region_id);
bool imdb_is_dax_enabled(void);
#else // CONFIG_DEV_DAX disabled
static inline int init_imdb_dax_dev(struct device *dev, int region_id)
{
	PNM_INF("CONFIG_DEV_DAX is disabled\n");
	return -ENXIO;
}

static inline bool imdb_is_dax_enabled(void)
{
	PNM_INF("CONFIG_DEV_DAX is disabled\n");
	return false;
}
#endif // CONFIG_DEV_DAX

#endif /* __IMDB_PRIVATE_H__ */
