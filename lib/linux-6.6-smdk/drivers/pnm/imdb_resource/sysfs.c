// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2023 Samsung LTD. All rights reserved.

#include "sysfs.h"

#include "allocator.h"
#include "private.h"
#include "proc_mgr.h"
#include "thread_sched.h"
#include "topo/export.h"
#include "topo/params.h"

#include "linux/types.h"
#include <linux/device.h>
#include <linux/imdb_resources.h>
#include <linux/kobject.h>
#include <linux/pnm/log.h>
#include <linux/slab.h>
#include <linux/stringify.h>
#include <linux/sysfs.h>

#define THREAD_ATTR_COUNT 1
#define WITH_NULL_TERM(var) ((var) + 1)
#define IMDB_RESET 1

static ssize_t cleanup_show(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	const uint64_t value = imdb_get_proc_manager();

	return sysfs_emit(buf, "%llu\n", value);
}

static ssize_t cleanup_store(struct device *device,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	int rc = 0;

	if (sysfs_streq(buf, __stringify(IMDB_DISABLE_CLEANUP))) {
		imdb_disable_cleanup();
	} else if (sysfs_streq(buf, __stringify(IMDB_ENABLE_CLEANUP))) {
		rc = imdb_enable_cleanup();
		if (rc) {
			PNM_ERR("Can't enable process manager\n");
			return rc;
		}
	}

	return count;
}
static DEVICE_ATTR_RW(cleanup);

static ssize_t leaked_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	const uint64_t value = imdb_get_leaked();

	return sysfs_emit(buf, "%llu\n", value);
}
static DEVICE_ATTR_RO(leaked);

static ssize_t mem_size_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	uint64_t value = imdb_get_mem_size();

	// [TODO: s-motov] - use sysfs-emit
	return sprintf(buf, "%llu\n", value);
}
static DEVICE_ATTR_RO(mem_size);

static ssize_t free_size_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	uint64_t value = get_avail_size();

	return sysfs_emit(buf, "%llu\n", value);
}
static DEVICE_ATTR_RO(free_size);

static ssize_t alignment_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	uint64_t value = get_granularity();

	return sysfs_emit(buf, "%llu\n", value);
}
static DEVICE_ATTR_RO(alignment);

static ssize_t reset_store(struct device *device, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int rc = 0;

	if (sysfs_streq(buf, __stringify(IMDB_RESET))) {
		rc = reset_memory_allocator();
		if (unlikely(rc)) {
			PNM_ERR("IMDB allocator reset failed\n");
			return rc;
		}

		reset_thread_sched();

		rc = imdb_reset_proc_manager();
		if (unlikely(rc)) {
			PNM_ERR("IMDB process manager reset failed\n");
			return rc;
		}
	}

	return count;
}
static DEVICE_ATTR_WO(reset);

static struct attribute *dev_attrs[] = {
	&dev_attr_cleanup.attr,
	&dev_attr_leaked.attr,
	&dev_attr_mem_size.attr,
	&dev_attr_free_size.attr,
	&dev_attr_alignment.attr,
	&dev_attr_reset.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.attrs = dev_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&dev_attr_group,
	NULL,
};

struct thread_attribute {
	struct attribute attr;
	uint8_t thread;
};

struct thread_sysfs {
	struct thread_attribute attrs[THREAD_ATTR_COUNT];
	struct attribute *attributes[WITH_NULL_TERM(THREAD_ATTR_COUNT)];
	struct attribute_group group;
	const struct attribute_group *groups[WITH_NULL_TERM(THREAD_ATTR_COUNT)];
	struct kobject thread_kobj;
} *thread_sysfs;

static const char *const thread_attr_name[] = {
	"state",
	NULL,
};

ssize_t thread_show(struct thread_attribute *attr, char *buf)
{
	bool state = false;

	if (strcmp(attr->attr.name, "state") == 0) {
		state = get_thread_state(attr->thread);
		return sprintf(buf, "%d\n", state);
	}

	PNM_ERR("Invalid Thread attribute\n");

	return 0;
}

static ssize_t thread_attr_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct thread_attribute *thread_attr =
		container_of(attr, struct thread_attribute, attr);

	return thread_show(thread_attr, buf);
}

static const struct sysfs_ops thread_sysfs_ops = {
	.show = thread_attr_show,
};

/* Operations on threads stats */
static struct kobj_type thread_type = {
	.sysfs_ops = &thread_sysfs_ops,
};

static int create_thread_kobject(struct thread_sysfs *tsysfs,
				 struct kobject *parent, uint8_t thread)
{
	int rc = 0;
	char name_buf[4];

	kobject_init(&tsysfs->thread_kobj, &thread_type);
	sprintf(name_buf, "%hhu", thread);

	rc = kobject_add(&tsysfs->thread_kobj, parent, name_buf);

	if (unlikely(rc))
		kobject_put(&tsysfs->thread_kobj);

	return rc;
}

static void fill_thread_sysfs(struct thread_sysfs *tsysfs, uint8_t thread)
{
	int attr_idx = 0;

	for (attr_idx = 0; attr_idx < THREAD_ATTR_COUNT; ++attr_idx) {
		tsysfs->attrs[attr_idx].attr.name = thread_attr_name[attr_idx];
		tsysfs->attrs[attr_idx].attr.mode = 0444;
		tsysfs->attrs[attr_idx].thread = thread;

		tsysfs->attributes[attr_idx] = &tsysfs->attrs[attr_idx].attr;
	}

	tsysfs->attributes[THREAD_ATTR_COUNT] = NULL;
}

static int build_thread_sysfs(struct kobject *parent)
{
	struct thread_sysfs *tsysfs = NULL;
	int rc = 0;
	uint8_t cunit = 0;

	thread_sysfs = kcalloc(imdb_topo()->nr_cunits,
			       sizeof(struct thread_sysfs), GFP_KERNEL);
	if (!thread_sysfs) {
		PNM_ERR("No free memory for cunits directories\n");
		return -ENOMEM;
	}

	for (cunit = 0; cunit < imdb_topo()->nr_cunits; ++cunit) {
		tsysfs = &thread_sysfs[cunit];
		rc = create_thread_kobject(tsysfs, parent, cunit);
		if (unlikely(rc)) {
			PNM_ERR("Can't create cunit kobject\n");
			return -EFAULT;
		}

		fill_thread_sysfs(tsysfs, cunit);

		tsysfs->group.attrs = tsysfs->attributes;
		tsysfs->groups[0] = &tsysfs->group;
		tsysfs->groups[1] = NULL;

		rc = sysfs_create_groups(&tsysfs->thread_kobj, tsysfs->groups);
		if (unlikely(rc)) {
			PNM_ERR("Can't create cunit group\n");
			return -EFAULT;
		}
	}

	return rc;
}

int imdb_build_sysfs(struct device *dev)
{
	int rc = 0;

	rc = sysfs_create_groups(&dev->kobj, attr_groups);
	if (unlikely(rc))
		return rc;

	rc = imdb_export_topology_constants(&dev->kobj);
	if (unlikely(rc))
		return rc;

	rc = build_thread_sysfs(&dev->kobj);

	return rc;
}

void imdb_destroy_sysfs(struct device *dev)
{
	struct kobject *kobj = &dev->kobj;
	struct thread_sysfs *tsysfs = NULL;
	uint8_t cunit = 0;

	for (cunit = 0; cunit < imdb_topo()->nr_cunits; ++cunit) {
		tsysfs = &thread_sysfs[cunit];
		sysfs_remove_groups(kobj, thread_sysfs->groups);
		kobject_del(&tsysfs->thread_kobj);
	}

	sysfs_remove_groups(kobj, attr_groups);
	kfree(thread_sysfs);
}
