/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __SLS__
#define __SLS__

#include <linux/types.h>

int init_sls_device(void);
struct device *get_sls_device(void);
void cleanup_sls_device(void);

// SLS DAX

#if IS_ENABLED(CONFIG_DEV_DAX)
int init_sls_dax_dev(struct device *dev, int region_id, int dax_id);
bool sls_is_dax_enabled(void);
#else // CONFIG_DEV_DAX disabled
static inline int init_sls_dax_dev(struct device *dev, int region_id,
				   int dax_id)
{
	PNM_INF("CONFIG_DEV_DAX is disabled\n");
	return -ENXIO;
}

static inline bool sls_is_dax_enabled(void)
{
	PNM_INF("CONFIG_DEV_DAX is disabled\n");
	return false;
}
#endif // CONFIG_DEV_DAX

#endif /* __SLS__ */
