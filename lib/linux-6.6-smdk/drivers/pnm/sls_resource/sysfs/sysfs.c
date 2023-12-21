// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#include "sysfs.h"
#include "allocator.h"
#include "cunit_sched.h"
#include "mem_info.h"
#include "private.h"
#include "process_manager.h"
#include "sysfs-private.h"
#include "topo/params.h"

#include <linux/kernel.h>
#include <linux/pnm/log.h>
#include <linux/sls_resources.h>
#include <linux/string.h>
#include <linux/types.h>

static ssize_t leaked_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	uint64_t leaked = sls_proc_mgr_leaked();

	return sysfs_emit(buf, "%llu\n", leaked);
}
static DEVICE_ATTR_RO(leaked);

static ssize_t cleanup_show(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	bool cleanup = sls_proc_mgr_cleanup();

	return sysfs_emit(buf, "%d\n", cleanup);
}

static ssize_t cleanup_store(struct device *device,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	if (sysfs_streq(buf, "1")) {
		if (sls_proc_manager_cleanup_on())
			PNM_ERR("Failed to enable resource manager\n");
	} else if (sysfs_streq(buf, "0")) {
		sls_proc_manager_cleanup_off();
	} else {
		PNM_DBG("Ignoring invalid value ('%s') written into sysfs 'cleanup' file\n",
			buf);
	}

	return count;
}
static DEVICE_ATTR_RW(cleanup);

static ssize_t acq_timeout_show(struct device *device,
				struct device_attribute *attr, char *buf)
{
	uint64_t acq_timeout = cunit_sched_acq_timeout();

	return sysfs_emit(buf, "%llu\n", acq_timeout);
}

static ssize_t acq_timeout_store(struct device *device,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	uint64_t acq_timeout;

	if (kstrtoull(buf, 10, &acq_timeout)) {
		PNM_ERR("Failed to convert cunit acquisition timeout string ('%s') to integer.\n",
			buf);
		return -EINVAL;
	}
	PNM_DBG("Setting acq_timeout to %llu ns via sysfs\n", acq_timeout);
	cunit_sched_set_acq_timeout(acq_timeout);
	return count;
}
static DEVICE_ATTR_RW(acq_timeout);

static ssize_t reset_store(struct device *device, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int rc = -EINVAL;

	if (!sysfs_streq(buf, "1")) {
		PNM_DBG("Ignoring invalid value ('%s') written into sysfs 'reset' file\n",
			buf);
		return count;
	}

	PNM_DBG("Resetting SLS device via sysfs\n");

	reset_cunit_sched();

	rc = reset_sls_allocator();
	if (unlikely(rc)) {
		PNM_ERR("Can't reset allocator\n");
		return rc;
	}

	reset_process_manager();

	return count;
}
static DEVICE_ATTR_WO(reset);

static ssize_t size_show(struct device *device, struct device_attribute *attr,
			 char *buf)
{
	const uint64_t value = sls_get_memory_size();

	return sysfs_emit(buf, "%llu\n", value);
}
static DEVICE_ATTR_RO(size);

static struct attribute *dev_attrs[] = {
	&dev_attr_acq_timeout.attr, &dev_attr_reset.attr, &dev_attr_leaked.attr,
	&dev_attr_cleanup.attr,	    &dev_attr_size.attr,  NULL,
};

static struct attribute_group dev_attr_group = {
	.attrs = dev_attrs,
};

static const struct attribute_group *dev_attr_groups[] = {
	&dev_attr_group,
	NULL,
};

int sls_build_sysfs(const struct sls_mem_cunit_info *mem_cunit_info,
		    const struct sls_mem_info *mem_info,
		    struct device *resource_dev)
{
	struct kobject *const parent = &resource_dev->kobj;
	int rc = 0;

	PNM_DBG("Building SLS sysfs\n");

	/* create statistics files */
	rc = sysfs_create_groups(parent, dev_attr_groups);
	if (unlikely(rc)) {
		PNM_ERR("Failed to create sysfs groups\n");
		goto out;
	}

	/* create cunits entry */
	rc = sls_build_cunits_sysfs(parent, mem_cunit_info);
	if (unlikely(rc)) {
		PNM_ERR("Failed to build cunits sysfs\n");
		goto group_cleanup;
	}

	/* create mappings */
	rc = sls_build_mappings_sysfs(parent, mem_info);
	if (unlikely(rc)) {
		PNM_ERR("Failed to build mappings sysfs\n");
		goto cunits_cleanup;
	}

	/* create topology entry */
	rc = sls_build_topology_sysfs(parent);
	if (unlikely(rc)) {
		PNM_ERR("Failed to build topology sysfs\n");
		goto mappings_free;
	}

	PNM_DBG("SLS sysfs is built\n");
	return rc;

mappings_free:
	sls_destroy_mappings_sysfs();
cunits_cleanup:
	sls_destroy_cunits_sysfs();
group_cleanup:
	sysfs_remove_groups(parent, dev_attr_groups);
out:
	return rc;
}

void sls_destroy_sysfs(struct device *dev)
{
	PNM_DBG("Destroying SLS sysfs\n");

	sysfs_remove_groups(&dev->kobj, dev_attr_groups);

	sls_destroy_cunits_sysfs();

	sls_destroy_mappings_sysfs();

	sls_destroy_topology_sysfs();

	PNM_DBG("SLS sysfs is destroyed\n");
}
