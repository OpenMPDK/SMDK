// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#include <linux/module.h>
#include <linux/pnm/log.h>

#include "cxl/cxl.h"
#include "device_resource.h"
#include "private.h"

static int cxl_imdb_probe(struct device *dev)
{
	int err = 0;

	PNM_INF("start imdb cxl probing\n");
	if (!imdb_is_dax_enabled())
		err = init_imdb_dax_dev(dev, PNM_DAX_MINOR_ID);

	return err;
}

static struct cxl_driver imdb_driver = { .name = "cxl_imdb",
					 .probe = cxl_imdb_probe,
					 .id = CXL_DEVICE_IMDB };

module_cxl_driver(imdb_driver);
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_IMDB);
