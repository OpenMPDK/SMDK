// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pnm/log.h>

#include "cxl/cxl.h"
#include "device_resource.h"
#include "sls.h"

static int sls_cxl_probe(struct device *dev)
{
	int err = 0;

	PNM_INF("start sls cxl probing\n");
	if (!sls_is_dax_enabled())
		err = init_sls_dax_dev(dev, PNM_DAX_MINOR_ID,
				       PNM_DAX_MAJOR_ID); // dax0.0
	return err;
}

static struct cxl_driver sls_cxl_driver = {
	.name = "sls_cxl",
	.probe = sls_cxl_probe,
	.id = CXL_DEVICE_PNM_SLS,
};

module_cxl_driver(sls_cxl_driver);
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_PNM_SLS);
