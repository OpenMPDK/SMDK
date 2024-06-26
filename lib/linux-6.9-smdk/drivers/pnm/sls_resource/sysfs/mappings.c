// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#include "private.h"
#include "sysfs-private.h"
#include "topo/params.h"

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pnm/log.h>
#include <linux/sls_resources.h>
#include <linux/string.h>
#include <linux/types.h>

struct raw_region_attribute {
	struct attribute attr;
	ssize_t (*show)(struct raw_region_attribute *attr, char *buf);
	uint8_t region;
};

struct raw_region_sysfs {
	struct raw_region_attribute attrs[RAW_REGION_SYSFS_ATTR_COUNT];
	struct attribute
		*attributes[WITH_NULL_TERM(RAW_REGION_SYSFS_ATTR_COUNT)];
	struct attribute_group group;
	const struct attribute_group *groups[WITH_NULL_TERM(SYSFS_GROUPS_COUNT)];
	struct kobject idx_kobj;
};

static struct kobject *mappings_kobj;
static struct raw_region_sysfs *raw_regions_fs;
static const struct sls_mem_info *mem_info;

static ssize_t nr_regions_show(struct device *device,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%llu\n", mem_info->nr_regions);
}
static DEVICE_ATTR_RO(nr_regions);

static struct attribute *mappings_attrs[] = {
	&dev_attr_nr_regions.attr,
	NULL,
};

static struct attribute_group mappings_attr_group = {
	.attrs = mappings_attrs,
};

static const struct attribute_group *mappings_attr_groups[] = {
	&mappings_attr_group,
	NULL,
};

static const char *const raw_region_attr_name[] = {
	"offset",
	"size",
	"type",
};

static ssize_t raw_region_show(struct raw_region_attribute *attr, char *buf)
{
	uint64_t offset, size;
	int type;

	if (strcmp(attr->attr.name, raw_region_attr_name[0]) == 0) {
		offset = mem_info->regions[attr->region].range.start;
		return sysfs_emit(buf, "%llu\n", offset);
	}

	if (strcmp(attr->attr.name, raw_region_attr_name[1]) == 0) {
		size = range_len(&mem_info->regions[attr->region].range);
		return sysfs_emit(buf, "%llu\n", size);
	}

	if (strcmp(attr->attr.name, raw_region_attr_name[2]) == 0) {
		type = mem_info->regions[attr->region].type;
		return sysfs_emit(buf, "%d\n", type);
	}

	return 0;
}

static ssize_t raw_region_attr_show(struct kobject *kobj,
				    struct attribute *attr, char *buf)
{
	struct raw_region_attribute *raw_region_attr =
		container_of(attr, struct raw_region_attribute, attr);

	if (!raw_region_attr->show)
		return -EIO;

	return raw_region_attr->show(raw_region_attr, buf);
}

static const struct sysfs_ops raw_region_sysfs_ops = {
	.show = raw_region_attr_show,
};

static struct kobj_type raw_region_type = {
	.sysfs_ops = &raw_region_sysfs_ops,
};

static void fill_raw_region_attrs(struct raw_region_sysfs *raw_region_fs,
				  uint8_t region)
{
	int attr_idx;

	for (attr_idx = 0; attr_idx < RAW_REGION_SYSFS_ATTR_COUNT; ++attr_idx) {
		raw_region_fs->attrs[attr_idx].attr.name =
			raw_region_attr_name[attr_idx];
		raw_region_fs->attrs[attr_idx].attr.mode = 0444;
		raw_region_fs->attrs[attr_idx].show = raw_region_show;
		raw_region_fs->attrs[attr_idx].region = region;

		raw_region_fs->attributes[attr_idx] =
			&raw_region_fs->attrs[attr_idx].attr;
	}

	raw_region_fs->attributes[RAW_REGION_SYSFS_ATTR_COUNT] = NULL;
}

static void fill_raw_region_sysfs(struct raw_region_sysfs *raw_region_fs,
				  uint8_t region)
{
	fill_raw_region_attrs(raw_region_fs, region);
	raw_region_fs->group.attrs = raw_region_fs->attributes;
	raw_region_fs->groups[0] = &raw_region_fs->group;
	raw_region_fs->groups[1] = NULL;
}

static int build_raw_region_sysfs(struct kobject *kobj, uint8_t region)
{
	char buf[4];
	int err;

	PNM_DBG("Building SLS sysfs for mappings region %hhu\n", region);

	kobject_init(&raw_regions_fs[region].idx_kobj, &raw_region_type);
	sprintf(buf, "%hhu", region);
	err = kobject_add(&raw_regions_fs[region].idx_kobj, kobj, buf);
	if (err)
		goto build_raw_region_kobject_out;

	fill_raw_region_sysfs(&raw_regions_fs[region], region);

	err = sysfs_create_groups(&raw_regions_fs[region].idx_kobj,
				  raw_regions_fs[region].groups);
	if (err)
		goto build_raw_region_out;

	return 0;

build_raw_region_out:
	kobject_del(&raw_regions_fs[region].idx_kobj);
build_raw_region_kobject_out:
	kobject_put(&raw_regions_fs[region].idx_kobj);
	memset(&raw_regions_fs[region].idx_kobj, 0, sizeof(struct kobject));
	return err;
}

int sls_build_mappings_sysfs(struct kobject *parent,
			     const struct sls_mem_info *meminfo)
{
	int err = 0;
	uint8_t region;

	PNM_DBG("Building SLS memory mappings sysfs\n");

	mem_info = meminfo;

	mappings_kobj = kobject_create_and_add(DEVICE_MAPPINGS_PATH, parent);

	if (!mappings_kobj) {
		PNM_ERR("Unable to create mappings sysfs kobject\n");
		err = -ENOMEM;
		goto out;
	}

	err = sysfs_create_groups(mappings_kobj, mappings_attr_groups);
	if (err) {
		PNM_ERR("Failed to create mappings sysfs groups\n");
		goto mappings_kobj_free;
	}

	raw_regions_fs = kcalloc(mem_info->nr_regions,
				 sizeof(struct raw_region_sysfs), GFP_KERNEL);
	if (!raw_regions_fs) {
		PNM_ERR("No free memory for raw regions directories\n");
		goto mappings_groups_free;
	}

	for (region = 0; region < mem_info->nr_regions; ++region) {
		err = build_raw_region_sysfs(mappings_kobj, region);
		if (err == 0)
			continue;

		PNM_ERR("Failed to build sysfs for mappings region [%hhu]\n",
			region);
		while (--region >= 0) {
			sysfs_remove_groups(&raw_regions_fs[region].idx_kobj,
					    raw_regions_fs[region].groups);
		}
		goto mappings_fs_free;
	}

	PNM_DBG("Built SLS mappings sysfs\n");
	return err;

mappings_fs_free:
	kfree(raw_regions_fs);
mappings_groups_free:
	sysfs_remove_groups(mappings_kobj, mappings_attr_groups);
mappings_kobj_free:
	kobject_del(mappings_kobj);
out:
	return err;
}

void sls_destroy_mappings_sysfs(void)
{
	PNM_DBG("Destroying SLS memory mappings sysfs\n");

	sysfs_remove_groups(mappings_kobj, mappings_attr_groups);
	kobject_del(mappings_kobj);

	PNM_DBG("Destroyed SLS memory mappings sysfs\n");
}
