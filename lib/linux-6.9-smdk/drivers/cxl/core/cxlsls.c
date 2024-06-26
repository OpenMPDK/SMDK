// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include "cxl.h"

struct cxl_sls_dev {
	struct device dev;
	struct cdev cdev;
	int id;
};

const struct device_type cxl_sls_type = {
	.name = "sls",
};

static const struct file_operations cxl_sls_fops = {
	.owner = THIS_MODULE,
};

bool is_cxl_sls(struct device *dev)
{
	return dev->type == &cxl_sls_type;
}

int cxl_add_sls(struct device *parent, int id)
{
	struct pci_dev *pdev;
	struct device *dev;
	struct cdev *cdev;
	struct cxl_sls_dev *sls_dev;
	int rc = 0;

	sls_dev = kzalloc(sizeof(struct cxl_sls_dev), GFP_KERNEL);
	if (!sls_dev)
		return -ENOMEM;

	pdev = to_pci_dev(parent);

	dev = &sls_dev->dev;
	device_initialize(dev);
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_sls_type;
	dev->parent = parent;
	dev->devt = 0xFFFFFFFF;

	cdev = &sls_dev->cdev;
	cdev_init(cdev, &cxl_sls_fops);

	rc = dev_set_name(dev, "sls%d", id);
	if (rc)
		goto cxl_add_sls_free;

	rc = cdev_device_add(cdev, dev);
	if (rc)
		goto cxl_add_sls_free;

	return rc;

cxl_add_sls_free:
	kfree(sls_dev);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_add_sls, CXL);
