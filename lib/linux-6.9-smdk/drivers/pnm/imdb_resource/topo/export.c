// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#include "export.h"
#include "params.h"

#include <linux/imdb_resources.h>

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pnm/log.h>

#define SINGLE_VALUE_ATTR(param, value)                                       \
	static ssize_t param##_show(struct device *device,                    \
				    struct device_attribute *attr, char *buf) \
	{                                                                     \
		return sysfs_emit(buf, "%d\n", value);                        \
	}                                                                     \
	static DEVICE_ATTR_RO(param)

#define EXPORT_PARAM(param) SINGLE_VALUE_ATTR(param, imdb_topo()->param)
#define DEV_ATTR_REF(param) (&dev_attr_##param.attr)

EXPORT_PARAM(nr_pools);
EXPORT_PARAM(nr_cunits);
EXPORT_PARAM(mem_size_gb);

static struct kobject *mem_topo_kobj;

static struct attribute *mem_topo_attrs[] = {
	DEV_ATTR_REF(nr_pools),
	DEV_ATTR_REF(nr_cunits),
	DEV_ATTR_REF(mem_size_gb),
	NULL,
};

static struct attribute_group mem_topo_attr_group = {
	.attrs = mem_topo_attrs,
};

static const struct attribute_group *mem_topo_attr_groups[] = {
	&mem_topo_attr_group,
	NULL,
};

int imdb_export_topology_constants(struct kobject *resource_kobj)
{
	PNM_DBG("Building IMDB memory topology sysfs\n");

	mem_topo_kobj = kobject_create_and_add(IMDB_DEVICE_TOPOLOGY_PATH,
					       resource_kobj);

	if (!mem_topo_kobj) {
		PNM_ERR("Unable to create topology sysfs kobject\n");
		return -ENOMEM;
	}

	if (sysfs_create_groups(mem_topo_kobj, mem_topo_attr_groups)) {
		PNM_ERR("Unable to create topology sysfs groups\n");
		kobject_del(mem_topo_kobj);
		return -ENOMEM;
	}

	PNM_DBG("Built IMDB memory topology sysfs\n");

	return 0;
}

void imdb_destroy_topology_constants(void)
{
	PNM_DBG("Destroying IMDB memory topology sysfs\n");

	if (!mem_topo_kobj) {
		PNM_ERR("Unexpected state of topology sysfs kobject (%p).\n",
			mem_topo_kobj);
		return;
	}

	sysfs_remove_groups(mem_topo_kobj, mem_topo_attr_groups);
	kobject_del(mem_topo_kobj);

	PNM_DBG("Destroyed IMDB memory topology sysfs\n");
}
