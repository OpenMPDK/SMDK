// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include "device_resource.h"

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/device/class.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pnm/log.h>
#include <linux/pnm_resources.h>

#define RESOURCE_ACCESS_MODE 0666

static struct class *pnm_device_class;

void pnm_destroy_resource_device(struct device *resource_device)
{
	if (IS_ERR_OR_NULL(resource_device))
		return;

	PNM_INF("Destroy %s device", dev_name(resource_device));
	device_destroy(pnm_device_class, resource_device->devt);
}
EXPORT_SYMBOL(pnm_destroy_resource_device);

int pnm_create_resource_device(char *resource_device_name, int *device_number,
			       struct device **resource_device)
{
	PNM_INF("Trying to create %s device...", resource_device_name);

	*resource_device = device_create(pnm_device_class, NULL, *device_number,
					 NULL, resource_device_name);

	if (IS_ERR(*resource_device)) {
		PNM_ERR("Fail to create %s device\n", resource_device_name);
		return PTR_ERR(*resource_device);
	}

	PNM_INF("%s device is created.", resource_device_name);

	return 0;
}
EXPORT_SYMBOL(pnm_create_resource_device);

static char *devnode_func(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = RESOURCE_ACCESS_MODE;

	return kasprintf(GFP_KERNEL, "%s/%s", PNM_RESOURCE_CLASS_NAME,
			 dev_name(dev));
}

static int __init init_pnm_class(void)
{
	pnm_device_class = class_create(PNM_RESOURCE_CLASS_NAME);
	if (IS_ERR(pnm_device_class)) {
		PNM_ERR("Failed to create PNM class\n");
		return PTR_ERR(pnm_device_class);
	}
	PNM_DBG("Created PNM class\n");

	pnm_device_class->devnode = devnode_func;

	return 0;
}

static void __exit exit_pnm_class(void)
{
	class_destroy(pnm_device_class);

	PNM_INF("PNM class resource manager unloaded.");
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PNM class resource manager");

module_init(init_pnm_class);
module_exit(exit_pnm_class);
