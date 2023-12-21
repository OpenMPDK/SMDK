// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#include "sysfs-private.h"
#include "topo/params.h"

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pnm/log.h>
#include <linux/sls_resources.h>

#define SINGLE_VALUE_ATTR(param, value)                                       \
	static ssize_t param##_show(struct device *device,                    \
				    struct device_attribute *attr, char *buf) \
	{                                                                     \
		return sysfs_emit(buf, "%d\n", value);                        \
	}                                                                     \
	static DEVICE_ATTR_RO(param)

#define EXPORT_PARAM(param) SINGLE_VALUE_ATTR(param, sls_topo()->param)
#define DEV_ATTR_REF(param) (&dev_attr_##param.attr)

EXPORT_PARAM(dev_type);
EXPORT_PARAM(nr_ranks);
EXPORT_PARAM(nr_cunits);
EXPORT_PARAM(nr_cs);
EXPORT_PARAM(nr_ch);
EXPORT_PARAM(aligned_tag_sz);
EXPORT_PARAM(inst_sz);
EXPORT_PARAM(data_sz);
EXPORT_PARAM(buf_sz);
EXPORT_PARAM(nr_inst_buf);
EXPORT_PARAM(nr_psum_buf);
EXPORT_PARAM(nr_tag_buf);
EXPORT_PARAM(reg_en);
EXPORT_PARAM(reg_exec);
EXPORT_PARAM(reg_poll);
EXPORT_PARAM(alignment_sz);

static ssize_t base_addr_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%llu\n", sls_topo()->base_addr);
}
DEVICE_ATTR_RO(base_addr);

static struct kobject *mem_topo_kobj;

static struct attribute *mem_topo_attrs[] = {
	DEV_ATTR_REF(dev_type),	   DEV_ATTR_REF(nr_ranks),
	DEV_ATTR_REF(nr_cunits),   DEV_ATTR_REF(nr_cs),
	DEV_ATTR_REF(nr_ch),	   DEV_ATTR_REF(aligned_tag_sz),
	DEV_ATTR_REF(inst_sz),	   DEV_ATTR_REF(data_sz),
	DEV_ATTR_REF(buf_sz),	   DEV_ATTR_REF(nr_inst_buf),
	DEV_ATTR_REF(nr_psum_buf), DEV_ATTR_REF(nr_tag_buf),
	DEV_ATTR_REF(reg_en),	   DEV_ATTR_REF(reg_exec),
	DEV_ATTR_REF(reg_poll),	   DEV_ATTR_REF(alignment_sz),
	DEV_ATTR_REF(base_addr),   NULL,
};

static struct attribute_group mem_topo_attr_group = {
	.attrs = mem_topo_attrs,
};

static const struct attribute_group *mem_topo_attr_groups[] = {
	&mem_topo_attr_group,
	NULL,
};

int sls_build_topology_sysfs(struct kobject *resource_kobj)
{
	int rc = 0;

	PNM_DBG("Building SLS memory topology sysfs\n");

	mem_topo_kobj =
		kobject_create_and_add(DEVICE_TOPOLOGY_PATH, resource_kobj);

	if (unlikely(!mem_topo_kobj)) {
		PNM_ERR("Unable to create topology sysfs kobject\n");
		rc = -ENOMEM;
		goto out;
	}

	rc = sysfs_create_groups(mem_topo_kobj, mem_topo_attr_groups);
	if (unlikely(rc)) {
		PNM_ERR("Unable to create topology sysfs groups\n");
		goto sysfs_group_free;
	}

	PNM_DBG("Built SLS memory topology sysfs\n");

	return rc;

sysfs_group_free:
	kobject_del(mem_topo_kobj);
out:
	return rc;
}

void sls_destroy_topology_sysfs(void)
{
	PNM_DBG("Destroying SLS memory topology sysfs\n");

	sysfs_remove_groups(mem_topo_kobj, mem_topo_attr_groups);
	kobject_del(mem_topo_kobj);

	PNM_DBG("Destroyed SLS memory topology sysfs\n");
}
