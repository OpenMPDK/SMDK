// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#include "allocator.h"
#include "cunit_sched.h"
#include "private.h"
#include "sysfs-private.h"
#include "topo/params.h"

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pnm/log.h>
#include <linux/sls_resources.h>
#include <linux/string.h>
#include <linux/types.h>

struct cunit_attribute {
	struct attribute attr;
	ssize_t (*show)(struct cunit_attribute *attr, char *buf);
	uint8_t cunit;
};

struct region_attribute {
	struct attribute attr;
	ssize_t (*show)(struct region_attribute *attr, char *buf);
	uint8_t cunit;
	uint8_t region;
};

struct regions_sysfs {
	struct region_attribute region_attrs[SLS_BLOCK_MAX]
					    [REGION_SYSFS_ATTR_COUNT];
	struct attribute
		*attrs[SLS_BLOCK_MAX][WITH_NULL_TERM(REGION_SYSFS_ATTR_COUNT)];
	struct attribute_group group[SLS_BLOCK_MAX];
	const struct attribute_group *groups[WITH_NULL_TERM(SLS_BLOCK_MAX)];
	struct kobject regions_kobj;
};

struct cunit_sysfs {
	struct cunit_attribute attrs[REGION_SYSFS_ATTR_COUNT];
	struct attribute *attributes[WITH_NULL_TERM(REGION_SYSFS_ATTR_COUNT)];
	struct attribute_group group;
	const struct attribute_group *groups[WITH_NULL_TERM(SYSFS_GROUPS_COUNT)];
	struct regions_sysfs regions_fs;
	struct kobject cunit_idx_kobj;
};

static struct cunit_sysfs *cunits_fs;
static struct kobject *cunits_kobj;
static const struct sls_mem_cunit_info *mem_cunit_info;

static ssize_t nr_cunits_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%llu\n", mem_cunit_info->nr_cunits);
}
static DEVICE_ATTR_RO(nr_cunits);

static struct attribute *cunits_attrs[] = {
	&dev_attr_nr_cunits.attr,
	NULL,
};

static struct attribute_group cunits_attr_group = {
	.attrs = cunits_attrs,
};

static const struct attribute_group *cunits_attr_groups[] = {
	&cunits_attr_group,
	NULL,
};

static const char *const cunit_attr_name[] = {
	"state",
	"size",
	"free_size",
	"acquisition_count",
};

static const char *const region_group_name[] = {
	"base", "inst", "cfgr", "tags", "psum",
};

static const char *const region_attr_name[] = {
	"size",
	"offset",
	"map_size",
	"map_offset",
};

static ssize_t cunit_show(struct cunit_attribute *attr, char *buf)
{
	uint8_t state;
	uint64_t free_size, size, acq_count;

	if (strcmp(attr->attr.name, cunit_attr_name[0]) == 0) {
		state = cunit_sched_state(attr->cunit);
		return sysfs_emit(buf, "%u\n", state);
	}

	if (strcmp(attr->attr.name, cunit_attr_name[1]) == 0) {
		size = get_total_size(attr->cunit);
		return sysfs_emit(buf, "%llu\n", size);
	}

	if (strcmp(attr->attr.name, cunit_attr_name[2]) == 0) {
		free_size = get_free_size(attr->cunit);
		return sysfs_emit(buf, "%llu\n", free_size);
	}

	if (strcmp(attr->attr.name, cunit_attr_name[3]) == 0) {
		acq_count = cunit_sched_acq_cnt(attr->cunit);
		return sysfs_emit(buf, "%llu\n", acq_count);
	}

	return 0;
}

static ssize_t cunit_attr_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct cunit_attribute *cunit_attr =
		container_of(attr, struct cunit_attribute, attr);

	if (!cunit_attr->show)
		return -EIO;

	return cunit_attr->show(cunit_attr, buf);
}

static ssize_t region_show(struct region_attribute *attr, char *buf)
{
	const size_t nr_cunits = mem_cunit_info->nr_cunits;
	const size_t nr_regions_per_cunit =
		mem_cunit_info->nr_regions / nr_cunits;
	const struct sls_mem_cunit_region *cunit_regions =
		&mem_cunit_info->regions[attr->cunit * nr_regions_per_cunit];
	const struct sls_mem_cunit_region *region = NULL;
	size_t idx;

	for (idx = 0; idx < nr_regions_per_cunit; ++idx)
		if (cunit_regions[idx].type == attr->region)
			region = &cunit_regions[idx];

	/* [TODO: @p.bred] Make logs here and in other places across the file */
	if (!region)
		return -EIO;

	if (strcmp(attr->attr.name, region_attr_name[0]) == 0)
		return sysfs_emit(buf, "%llu\n", range_len(&region->range));

	if (strcmp(attr->attr.name, region_attr_name[1]) == 0)
		return sysfs_emit(buf, "%llu\n", region->range.start);

	if (strcmp(attr->attr.name, region_attr_name[2]) == 0)
		return sysfs_emit(buf, "%llu\n", range_len(&region->map_range));

	if (strcmp(attr->attr.name, region_attr_name[3]) == 0)
		return sysfs_emit(buf, "%llu\n", region->map_range.start);

	return -EIO;
}

static ssize_t region_attr_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct region_attribute *region_attr =
		container_of(attr, struct region_attribute, attr);

	if (!region_attr->show)
		return -EIO;

	return region_attr->show(region_attr, buf);
}

static void fill_region_sysfs(struct regions_sysfs *regions_fs, uint8_t cunit,
			      uint8_t region)
{
	int attr_counter;
	struct region_attribute *reg_attr;

	for (attr_counter = 0; attr_counter < REGION_SYSFS_ATTR_COUNT;
	     ++attr_counter) {
		reg_attr = &regions_fs->region_attrs[region][attr_counter];
		reg_attr->attr.name = region_attr_name[attr_counter];
		reg_attr->attr.mode = 0444;
		reg_attr->show = region_show;
		reg_attr->cunit = cunit;
		reg_attr->region = region;

		regions_fs->attrs[region][attr_counter] =
			&regions_fs->region_attrs[region][attr_counter].attr;
	}

	regions_fs->attrs[region][REGION_SYSFS_ATTR_COUNT] = NULL;
	regions_fs->group[region].name = region_group_name[region];
	regions_fs->group[region].attrs = &regions_fs->attrs[region][0];

	regions_fs->groups[region] = &regions_fs->group[region];
}

static void fill_regions_sysfs(struct regions_sysfs *regions_fs, uint8_t cunit)
{
	int region;

	for (region = 0; region < SLS_BLOCK_MAX; region++)
		fill_region_sysfs(regions_fs, cunit, region);

	regions_fs->groups[SLS_BLOCK_MAX] = NULL;
}

static const struct sysfs_ops regions_sysfs_ops = {
	.show = region_attr_show,
};

/* Operations on memory regions inside cunit */
static struct kobj_type regions_type = {
	.sysfs_ops = &regions_sysfs_ops,
};

static int build_cunit_regions_sysfs(struct kobject *kobj,
				     struct regions_sysfs *regions_fs,
				     uint8_t cunit)
{
	int err;

	kobject_init(&regions_fs->regions_kobj, &regions_type);
	err = kobject_add(&regions_fs->regions_kobj, kobj, "regions");
	if (err) {
		kobject_put(&regions_fs->regions_kobj);
		memset(&regions_fs->regions_kobj, 0,
		       sizeof(regions_fs->regions_kobj));
		goto cunit_regions_out;
	}

	fill_regions_sysfs(regions_fs, cunit);

	err = sysfs_create_groups(&regions_fs->regions_kobj,
				  regions_fs->groups);
	if (err)
		goto cunit_regions_out;

cunit_regions_out:
	return err;
}

static void fill_cunit_attrs(struct cunit_sysfs *cunit_fs, uint8_t cunit)
{
	int attr_idx;

	for (attr_idx = 0; attr_idx < REGION_SYSFS_ATTR_COUNT; ++attr_idx) {
		cunit_fs->attrs[attr_idx].attr.name = cunit_attr_name[attr_idx];
		cunit_fs->attrs[attr_idx].attr.mode = 0444;
		cunit_fs->attrs[attr_idx].show = cunit_show;
		cunit_fs->attrs[attr_idx].cunit = cunit;

		cunit_fs->attributes[attr_idx] =
			&cunit_fs->attrs[attr_idx].attr;
	}

	cunit_fs->attributes[REGION_SYSFS_ATTR_COUNT] = NULL;
}

static void fill_cunit_sysfs(struct cunit_sysfs *cunit_fs, uint8_t cunit)
{
	fill_cunit_attrs(cunit_fs, cunit);
	cunit_fs->group.attrs = cunit_fs->attributes;
	cunit_fs->groups[0] = &cunit_fs->group;
	cunit_fs->groups[1] = NULL;
}

static const struct sysfs_ops cunit_sysfs_ops = {
	.show = cunit_attr_show,
};

/* Operations on cunit stats */
static const struct kobj_type cunit_type = {
	.sysfs_ops = &cunit_sysfs_ops,
};

static int build_cunit_sysfs(struct kobject *kobj, uint8_t cunit)
{
	char buf[4];
	int err;

	PNM_DBG("Building SLS sysfs for cunit %hhu\n", cunit);

	kobject_init(&cunits_fs[cunit].cunit_idx_kobj, &cunit_type);
	sprintf(buf, "%hhu", cunit);
	err = kobject_add(&cunits_fs[cunit].cunit_idx_kobj, kobj, buf);
	if (err) {
		kobject_put(&cunits_fs[cunit].cunit_idx_kobj);
		memset(&cunits_fs[cunit].cunit_idx_kobj, 0,
		       sizeof(struct kobject));
		goto build_cunit_out;
	}

	fill_cunit_sysfs(&cunits_fs[cunit], cunit);

	if (sysfs_create_groups(&cunits_fs[cunit].cunit_idx_kobj,
				cunits_fs[cunit].groups)) {
		err = -ENOMEM;
		goto build_cunit_out;
	}

	err = build_cunit_regions_sysfs(&cunits_fs[cunit].cunit_idx_kobj,
					&cunits_fs[cunit].regions_fs, cunit);

build_cunit_out:
	return err;
}

int sls_build_cunits_sysfs(struct kobject *parent,
			   const struct sls_mem_cunit_info *memcunit_info)
{
	int rc = 0;
	uint8_t cunit;

	PNM_DBG("Start building sls cunits sysfs\n");

	mem_cunit_info = memcunit_info;

	cunits_kobj = kobject_create_and_add(DEVICE_CUNITS_PATH, parent);

	if (unlikely(!cunits_kobj)) {
		PNM_ERR("Failed to create cunits kobject\n");
		rc = -ENOMEM;
		goto out;
	}

	rc = sysfs_create_groups(cunits_kobj, cunits_attr_groups);
	if (unlikely(rc)) {
		PNM_ERR("Failed to add attributes to cunits entry\n");
		goto cunits_kobj_free;
	}

	cunits_fs = kcalloc(sls_topo()->nr_cunits, sizeof(struct cunit_sysfs),
			    GFP_KERNEL);
	if (unlikely(!cunits_fs)) {
		PNM_ERR("No free memory for cunits directories\n");
		goto cunits_groups_free;
	}

	for (cunit = 0; cunit < sls_topo()->nr_cunits; ++cunit) {
		rc = build_cunit_sysfs(cunits_kobj, cunit);
		if (likely(rc == 0))
			continue;

		PNM_ERR("Failed to build sysfs for cunit [%d]\n", cunit);
		while (--cunit >= 0) {
			sysfs_remove_groups(
				&cunits_fs[cunit].regions_fs.regions_kobj,
				cunits_fs[cunit].regions_fs.groups);
			sysfs_remove_groups(&cunits_fs[cunit].cunit_idx_kobj,
					    cunits_fs[cunit].groups);
		}
		goto cunits_free;
	}

	return rc;

cunits_free:
	kfree(cunits_fs);
cunits_groups_free:
	sysfs_remove_groups(cunits_kobj, cunits_attr_groups);
cunits_kobj_free:
	kobject_del(cunits_kobj);
out:
	return rc;
}

void sls_destroy_cunits_sysfs(void)
{
	uint8_t cunit;

	for (cunit = 0; cunit < sls_topo()->nr_cunits; ++cunit) {
		sysfs_remove_groups(&cunits_fs[cunit].regions_fs.regions_kobj,
				    cunits_fs[cunit].regions_fs.groups);
		kobject_del(&cunits_fs[cunit].regions_fs.regions_kobj);
		sysfs_remove_groups(cunits_kobj, cunits_fs[cunit].groups);
		kobject_del(&cunits_fs[cunit].cunit_idx_kobj);
	}

	kobject_del(cunits_kobj);
	kfree(cunits_fs);
}
