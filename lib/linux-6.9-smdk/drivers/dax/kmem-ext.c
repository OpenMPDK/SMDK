// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016-2019 Intel Corporation. All rights reserved. */
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pfn_t.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/memory-tiers.h>
#include <linux/nodemask.h>
#include <linux/range.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/bitmap.h>
#include <linux/memory_hotplug.h>
#include "dax-private.h"
#include "bus.h"
#include "../cxl/cxl.h"
#include "../cxl/cxlmem.h"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " " fmt

#define DEV_NAME	"tierd"

#define TIERD_REGISTER_TASK			_IO('M', 0)
#define TIERD_UNREGISTER_TASK		_IO('M', 1)

static struct class *dev_class;
static dev_t tierd_devt;
static struct cdev tierd_cdev;
static struct task_struct *tierd_task;
static DEFINE_MUTEX(off_on_lock);

static int device_open(struct inode *inode, struct file *file)
{
	if (!try_module_get(THIS_MODULE)) {
		pr_err("Failed to get module\n");
		return -ENODEV;
	}

	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
	module_put(THIS_MODULE);
	return 0;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case TIERD_REGISTER_TASK:
		tierd_task = current;
		pr_info("Register pid %d\n", tierd_task->pid);
		break;
	case TIERD_UNREGISTER_TASK:
		if (!tierd_task) {
			pr_err("task is not registered yet\n");
			return -EPERM;
		}
		if (tierd_task->pid != current->pid) {
			pr_err("Current task(%d) is different from registered pid(%d)\n",
					current->pid, tierd_task->pid);
			return -EPERM;
		}

		pr_info("Unregister task (pid: %d)\n", tierd_task->pid);
		tierd_task = NULL;
		break;
	default:
		pr_err("Not supported command: %x\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static struct file_operations fops = {
	.open = device_open,
	.release = device_release,
	.unlocked_ioctl = device_ioctl,
};

static int tierd_memory_notifier(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct memory_notify *mem = (struct memory_notify *)v;
	int rc;

	if (!tierd_task) {
		pr_warn_once("Task is not registered\n");
		return NOTIFY_DONE;
	}

	switch (val) {
	case MEM_OFFLINE:
	case MEM_ONLINE:
		if (mem->status_change_nid == -1)
			return NOTIFY_DONE;

		pr_info("Node %d status changed\n", mem->status_change_nid);
		rc = kill_pid(find_vpid(tierd_task->pid), SIGUSR1, 1);
		if (rc) {
			pr_err("Failed to send signal %d to pid %d\n",
					SIGUSR1, tierd_task->pid);
			return NOTIFY_DONE;
		}

		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block tierd_memory_nb = {
	.notifier_call = tierd_memory_notifier,
	.priority = 0
};

static int __init tierd_init(void)
{
	int rc;
	struct device *dev;

	rc = register_memory_notifier(&tierd_memory_nb);
	if (rc) {
		pr_err("Failed to register a memory notifier (%d)\n", rc);
		return rc;
	}

	rc = alloc_chrdev_region(&tierd_devt, 0, 1, DEV_NAME);
	if (rc) {
		pr_err("Failed to allocate chrdev region (%d)\n", rc);
		goto err_chrdev_region;
	}

	cdev_init(&tierd_cdev, &fops);

	rc = cdev_add(&tierd_cdev, tierd_devt, 1);
	if (rc) {
		pr_err("Failed to add device to the system (%d)\n", rc);
		goto err_cdev;
	}

	dev_class = class_create(DEV_NAME);
	if (IS_ERR(dev_class)) {
		rc = PTR_ERR(dev_class);
		pr_err("Failed to create class (%d)\n", rc);
		goto err_class;
	}

	dev = device_create(dev_class, NULL, tierd_devt, NULL, DEV_NAME);
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		pr_err("Failed to create device (%d)\n", rc);
		goto err_device;
	}

	return 0;

err_device:
	class_destroy(dev_class);
err_class:
	cdev_del(&tierd_cdev);
err_cdev:
	unregister_chrdev_region(tierd_devt, 1);
err_chrdev_region:
	unregister_memory_notifier(&tierd_memory_nb);

	return rc;
}

static void __exit tierd_exit(void)
{
	device_destroy(dev_class, tierd_devt);
	class_destroy(dev_class);
	cdev_del(&tierd_cdev);
	unregister_chrdev_region(tierd_devt, 1);
	unregister_memory_notifier(&tierd_memory_nb);
}

#define KOBJ_RELEASE(_name) \
	{ kobject_put(_name); _name = NULL;}
#define SMDK_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define SMDK_ATTR(_name) \
	static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

#define SMDK_DEV_NAME_MAX (16)
#define CXL_MEM_DEV_NAME_MAX (16)
struct cxl_device {
	char name[SMDK_DEV_NAME_MAX];
	struct kobject *kobj;
	u64 start;
	u64 size;
	int nid;
	int tid;
	int mdid[CXL_DECODER_MAX_INTERLEAVE];
	int num_mds;
	struct list_head list;
	struct dev_dax *dev_dax;
};

struct cxl_devs {
	int num_of_devs;
	DECLARE_BITMAP(probe, MAX_NUMNODES);
	DECLARE_BITMAP(active, MAX_NUMNODES);
	struct cxl_device device[MAX_NUMNODES];
};

static struct kobject *cxl_kobj;
static struct kobject *cxl_devices_kobj;

static struct cxl_devs cxl_devs;
static DEFINE_MUTEX(cxl_devs_lock);

struct cxl_node {
	int id;
	int num_of_devs;
	struct kobject *kobj;
	struct list_head devs;
	struct list_head entry;
};

static struct kobject *cxl_nodes_kobj;
static LIST_HEAD(cxl_node_list);

static struct cxl_node *cxl_node_find_alloc(int tid);
static struct cxl_node *cxl_node_find(int tid);
static struct cxl_node *cxl_node_alloc(int tid);
static void cxl_node_free(int tid);

/* from kmem.c */
/*
 * Default abstract distance assigned to the NUMA node onlined
 * by DAX/kmem if the low level platform driver didn't initialize
 * one for this NUMA node.
 */
#define MEMTIER_DEFAULT_DAX_ADISTANCE	(MEMTIER_ADISTANCE_DRAM * 5)

/* Memory resource name used for add_memory_driver_managed(). */
static const char *kmem_name;
/* Set if any memory will remain added when the driver will be unloaded. */
static bool any_hotremove_failed;

static int dax_kmem_range(struct dev_dax *dev_dax, int i, struct range *r)
{
	struct dev_dax_range *dax_range = &dev_dax->ranges[i];
	struct range *range = &dax_range->range;

	/* memory-block align the hotplug range */
	r->start = ALIGN(range->start, memory_block_size_bytes());
	r->end = ALIGN_DOWN(range->end + 1, memory_block_size_bytes()) - 1;
	if (r->start >= r->end) {
		r->start = range->start;
		r->end = range->end;
		return -ENOSPC;
	}
	return 0;
}

struct dax_kmem_data {
	const char *res_name;
	int mgid;
	struct resource *res[];
};

static struct memory_dev_type *dax_slowmem_type;

/* SMDK devices */
static struct cxl_device *kobj_to_cxldev(struct kobject *kobj)
{
	int p;

	for_each_set_bit(p, cxl_devs.probe, MAX_NUMNODES) {
		if (cxl_devs.device[p].kobj == kobj)
			return &cxl_devs.device[p];
	}

	return NULL;
}

static struct cxl_device *dev_dax_to_cxldev(struct dev_dax *dev_dax)
{
	int p;
	struct cxl_device *cxldev;

	for_each_set_bit(p, cxl_devs.probe, MAX_NUMNODES) {
		cxldev = &cxl_devs.device[p];
		if (cxldev->dev_dax == dev_dax &&
		    cxldev->dev_dax->region->id == dev_dax->region->id)
			return &cxl_devs.device[p];
	}

	return NULL;
}

static ssize_t start_address_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	struct cxl_device *cxldev;

	cxldev = kobj_to_cxldev(kobj);
	if (!cxldev) {
		pr_err("failed to get cxl device\n");
		return -EINVAL;
	}

	return sysfs_emit(buf, "0x%llx\n", cxldev->start);
}
SMDK_ATTR_RO(start_address);

static ssize_t size_show(struct kobject *kobj,
			 struct kobj_attribute *attr,
			 char *buf)
{
	struct cxl_device *cxldev;

	cxldev = kobj_to_cxldev(kobj);
	if (!cxldev) {
		pr_err("failed to get cxl device\n");
		return -EINVAL;
	}

	return sysfs_emit(buf, "0x%llx\n", cxldev->size);
}
SMDK_ATTR_RO(size);

static ssize_t node_id_show(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    char *buf)
{
	struct cxl_device *cxldev;

	cxldev = kobj_to_cxldev(kobj);
	if (!cxldev) {
		pr_err("failed to get cxl device\n");
		return -EINVAL;
	}

	return sysfs_emit(buf, "%d\n", cxldev->tid);
}

static u64 cxl_dax_kmem_start(struct dev_dax *dev_dax)
{
	struct dev_dax_range *dax_range = &dev_dax->ranges[0];
	struct range *range = &dax_range->range;

	return ALIGN(range->start, memory_block_size_bytes());
}

static u64 cxl_dax_kmem_size(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	u64 size = 0;
	int i, rc;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc) {
			dev_info(dev, "mapping%d: %#llx-%#llx too small after alignment\n",
					i, range.start, range.end);
			continue;
		}
		size += range_len(&range);
	}

	return size;
}

static int cxl_dev_dax_kmem_add(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	unsigned long total_len = 0;
	struct dax_kmem_data *data = dev_get_drvdata(dev);
	int i, rc, mapped = 0;
	int numa_node;

	numa_node = dev_dax->target_node;
	if (numa_node < 0) {
		dev_warn(dev, "rejecting DAX region with invalid node: %d\n",
				numa_node);
		return -EINVAL;
	}

	total_len = cxl_dax_kmem_size(dev_dax);
	if (!total_len) {
		dev_warn(dev, "rejecting DAX region without any memory after alignment\n");
		return -EINVAL;
	}

	init_node_memory_type(numa_node, dax_slowmem_type);

	rc = memory_group_register_static(numa_node, PFN_UP(total_len));
	if (rc < 0)
		goto err_reg_mgid;
	data->mgid = rc;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct resource *res;
		struct range range;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc)
			continue;

		res = request_mem_region(range.start, range_len(&range), data->res_name);
		if (!res) {
			dev_warn(dev, "mapping%d: %#llx-%#llx could not reserve region\n",
					i, range.start, range.end);
			if (mapped)
				continue;
			rc = -EBUSY;
			goto err_request_mem;
		}
		data->res[i] = res;

		res->flags = IORESOURCE_SYSTEM_RAM;

		rc = add_subzone(numa_node, range.start, range.end);
		if (rc) {
			dev_warn(dev, "mapping%d: %#llx-%#llx subzone add failed\n",
					i, range.start, range.end);
			remove_resource(res);
			kfree(res);
			data->res[i] = NULL;
			if (mapped)
				continue;
			goto err_request_mem;
		}

		rc = add_memory_driver_managed(data->mgid, range.start,
				range_len(&range), kmem_name, MHP_NID_IS_MGID);
		if (rc) {
			dev_warn(dev, "mapping%d: %#llx-%#llx memory add failed\n",
					i, range.start, range.end);
			remove_subzone(numa_node, range.start, range.end);
			remove_resource(res);
			kfree(res);
			data->res[i] = NULL;
			if (mapped)
				continue;
			goto err_request_mem;
		}

		dev_info(dev, "mapping%d: %#llx-%#llx memory added\n",
				i, range.start, range.end);

		mapped++;
	}

	return 0;

err_request_mem:
	memory_group_unregister(data->mgid);
err_reg_mgid:
	clear_node_memory_type(numa_node, dax_slowmem_type);
	return rc;
}

static void cxl_dev_dax_kmem_online(struct dev_dax *dev_dax)
{
	int i;
	struct range range;

	if (dev_dax->target_node != NUMA_NO_NODE) {
		for (i = 0; i < dev_dax->nr_range; i++) {
			if (dax_kmem_range(dev_dax, i, &range))
				continue;

			online_movable_memory(range.start, range_len(&range));
		}
	}
}

static int cxl_dev_dax_kmem_offline_and_remove(struct dev_dax *dev_dax)
{
	int i, success = 0;
	int node = dev_dax->target_node;
	struct device *dev = &dev_dax->dev;
	struct dax_kmem_data *data = dev_get_drvdata(dev);

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;
		int rc;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc)
			continue;

		rc = offline_and_remove_memory(range.start, range_len(&range));
		if (rc == 0) {
			rc = remove_subzone(node, range.start, range.end);
			if (rc)
				dev_warn(dev, "mapping%d: %#llx-%#llx remove subzone failed\n",
						i, range.start, range.end);
			remove_resource(data->res[i]);
			kfree(data->res[i]);
			data->res[i] = NULL;
			success++;
			continue;
		}
		dev_err(dev,
			"mapping%d: %#llx-%#llx cannot be hotremoved until the next reboot\n",
				i, range.start, range.end);
	}

	if (success >= dev_dax->nr_range) {
		memory_group_unregister(data->mgid);
		clear_node_memory_type(node, dax_slowmem_type);
		return 0;
	}

	return -EBUSY;
}

static ssize_t node_id_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t len)
{
	struct cxl_device *cxldev;
	struct cxl_node *cxlnode;
	int tid;
	int ret;

	cxldev = kobj_to_cxldev(kobj);
	if (!cxldev) {
		pr_err("failed to get cxl device\n");
		return -EINVAL;
	}

	if (!test_bit(cxldev->nid, cxl_devs.active)) {
		pr_err("cxl device is not active\n");
		return -EINVAL;
	}

	if (kstrtoint(buf, 10, &tid)) {
		pr_err("failed to parse target node ID\n");
		return -EINVAL;
	}

	if (tid != NUMA_NO_NODE) {
		if ((tid >= MAX_NUMNODES) || !test_bit(tid, cxl_devs.probe)) {
			pr_err("failed to find target node\n");
			return -EINVAL;
		}

		if (cxldev->tid == tid) {
			pr_warn("device is already in target node\n");
			return len;
		} else if (cxldev->tid != NUMA_NO_NODE) {
			cxlnode = cxl_node_find(cxldev->tid);
			if (!cxlnode) {
				pr_err("failed to find cxl node\n");
				return -EINVAL;
			}

			sysfs_remove_link(cxlnode->kobj, cxldev->name);

			list_del(&cxldev->list);
			cxlnode->num_of_devs--;
			cxl_node_free(cxldev->tid);
		}

		cxlnode = cxl_node_find_alloc(tid);
		if (!cxlnode) {
			pr_err("failed to get cxl node\n");
			return -EINVAL;
		}

		ret = sysfs_create_link_nowarn(cxlnode->kobj, cxldev->kobj, cxldev->name);
		if (ret) {
			pr_err("failed to create link between node%d and %s\n",
							cxlnode->id, cxldev->name);
			return ret;
		}

		list_add_tail(&cxldev->list, &cxlnode->devs);
		cxlnode->num_of_devs++;

		mutex_lock(&off_on_lock);
		ret = cxl_dev_dax_kmem_offline_and_remove(cxldev->dev_dax);
		if (!ret) {
			cxldev->tid = tid;
			cxldev->dev_dax->target_node = cxldev->tid;
			cxldev->dev_dax->region->target_node = cxldev->tid;

			cxl_dev_dax_kmem_add(cxldev->dev_dax);
			cxl_dev_dax_kmem_online(cxldev->dev_dax);
		}
		mutex_unlock(&off_on_lock);
	} else {
		if (cxldev->tid == NUMA_NO_NODE) {
			pr_warn("device is offlined already\n");
			return len;
		}

		cxlnode = cxl_node_find(cxldev->tid);
		if (!cxlnode) {
			pr_err("failed to find cxl node\n");
			return -EINVAL;
		}

		sysfs_remove_link(cxlnode->kobj, cxldev->name);

		list_del(&cxldev->list);
		cxlnode->num_of_devs--;
		cxl_node_free(cxldev->tid);

		mutex_lock(&off_on_lock);
		ret = cxl_dev_dax_kmem_offline_and_remove(cxldev->dev_dax);
		if (!ret) {
			cxldev->tid = NUMA_NO_NODE;
			cxldev->dev_dax->target_node = cxldev->nid;
			cxldev->dev_dax->region->target_node = cxldev->nid;

			cxl_dev_dax_kmem_add(cxldev->dev_dax);
		}
		mutex_unlock(&off_on_lock);
	}

	return len;
}
SMDK_ATTR(node_id);

static ssize_t socket_id_show(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	struct cxl_device *cxldev;

	cxldev = kobj_to_cxldev(kobj);
	if (!cxldev) {
		pr_err("failed to get cxl device\n");
		return -EINVAL;
	}

	return sysfs_emit(buf, "%d\n", dev_to_node(&cxldev->dev_dax->dev));
}
SMDK_ATTR_RO(socket_id);

static ssize_t state_show(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    char *buf)
{
	struct cxl_device *cxldev;

	cxldev = kobj_to_cxldev(kobj);
	if (!cxldev) {
		pr_err("failed to get cxl device\n");
		return -EINVAL;
	}

	if (test_bit(cxldev->nid, cxl_devs.active) &&
			cxldev->tid != NUMA_NO_NODE)
		return sysfs_emit(buf, "online\n");

	return sysfs_emit(buf, "offline\n");
}
SMDK_ATTR_RO(state);

static struct attribute *cxl_device_attrs[] = {
	&start_address_attr.attr,
	&size_attr.attr,
	&node_id_attr.attr,
	&socket_id_attr.attr,
	&state_attr.attr,
	NULL,
};

static const struct attribute_group cxl_device_attr_group = {
	.attrs = cxl_device_attrs,
};

/* SMDK nodes */
static struct cxl_node *cxl_node_find_alloc(int tid)
{
	struct cxl_node *cxlnode;

	cxlnode = cxl_node_find(tid);
	if (!cxlnode)
		cxlnode = cxl_node_alloc(tid);

	return cxlnode;
}

static struct cxl_node *cxl_node_find(int tid)
{
	struct cxl_node *pos;

	list_for_each_entry(pos, &cxl_node_list, entry) {
		if (pos->id == tid)
			return pos;
	}

	return NULL;
}

static struct cxl_node *cxl_node_alloc(int tid)
{
	struct cxl_node *cxlnode;
	char *nname;

	cxlnode = kzalloc(sizeof(*cxlnode), GFP_KERNEL);
	if (!cxlnode)
		return ERR_PTR(-ENOMEM);

	cxlnode->id = tid;

	INIT_LIST_HEAD(&cxlnode->devs);
	INIT_LIST_HEAD(&cxlnode->entry);

	nname = __getname();
	if (!nname)  {
		kfree(cxlnode);
		return ERR_PTR(-ENOMEM);
	}

	scnprintf(nname, PATH_MAX, "node%d", tid);
	cxlnode->kobj = kobject_create_and_add(nname, cxl_nodes_kobj);
	if (!cxlnode->kobj) {
		pr_err("failed to create cxl node");
		kfree(cxlnode);
		__putname(nname);
		return ERR_PTR(-ENOMEM);
	}

	__putname(nname);

	list_add_tail(&cxlnode->entry, &cxl_node_list);

	pr_debug("allocated node%d\n", cxlnode->id);

	return cxlnode;
}

static void cxl_node_free(int tid)
{
	struct cxl_node *cxlnode = cxl_node_find(tid);

	if (cxlnode && !cxlnode->num_of_devs) {
		list_del(&cxlnode->entry);
		KOBJ_RELEASE(cxlnode->kobj);
		kfree(cxlnode);
	}
}

static void pre_dev_dax_kmem_probe(struct dev_dax *dev_dax)
{
	struct cxl_device *cxldev = dev_dax_to_cxldev(dev_dax);
	int nid = cxldev ? cxldev->nid : dev_dax->target_node;

	if (test_bit(nid, cxl_devs.probe) &&
			(cxldev->tid != cxldev->nid)) {
		if (cxldev->tid != NUMA_NO_NODE) {
			dev_dax->target_node = cxldev->tid;
			dev_dax->region->target_node = cxldev->tid;
		} else {
			cxldev->tid = cxldev->nid;
		}
	}
}

static void post_dev_dax_kmem_probe(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	int rid = dev_dax->region->id;
	struct cxl_device *cxldev = dev_dax_to_cxldev(dev_dax);
	int nid = cxldev ? cxldev->nid : dev_dax->target_node;
	struct cxl_node *cxlnode;
	struct cxl_dax_region *cxlr_dax;

	/* new dev_dax */
	if (!test_bit(nid, cxl_devs.probe)) {
		cxldev = &cxl_devs.device[nid];

		scnprintf(cxldev->name, SMDK_DEV_NAME_MAX, "cxl%d", rid);
		cxldev->kobj = kobject_create_and_add(cxldev->name, cxl_devices_kobj);
		if (!cxldev->kobj) {
			pr_err("failed to create cxl device");
			return;
		}

		if (sysfs_create_link_nowarn(cxldev->kobj, &dev->kobj, dev_name(dev)))
			pr_err("failed to create link between cxl%d and %s\n",
							rid, dev_name(dev));

		cxldev->nid = nid;
		cxldev->tid = nid;
		INIT_LIST_HEAD(&cxldev->list);
		cxldev->dev_dax = dev_dax;

		set_bit(nid, cxl_devs.probe);
		cxl_devs.num_of_devs++;

		if (sysfs_create_group(cxldev->kobj, &cxl_device_attr_group))
			pr_err("failed to create cxl device attribute\n");

		cxlr_dax = dev_dax->region->dev->type ?
			to_cxl_dax_region(dev_dax->region->dev) : NULL;
		if (cxlr_dax) {
			struct cxl_region_params *p = &cxlr_dax->cxlr->params;

			for (int i = 0; i < p->nr_targets; i++) {
				struct cxl_memdev *cxlmd = cxled_to_memdev(p->targets[i]);
				char mdname[CXL_MEM_DEV_NAME_MAX];
				cxldev->mdid[cxldev->num_mds++] = cxlmd->id;
				scnprintf(mdname, sizeof(mdname), "mem%d", cxlmd->id);
				if (sysfs_create_link_nowarn(cxldev->kobj, &cxlmd->dev.kobj, mdname))
					pr_err("failed to create cxl device's memdev symlink\n");
			}
		}
	} else {
		cxldev = dev_dax_to_cxldev(dev_dax);
	}

	cxlnode = cxl_node_find_alloc(nid);
	if (cxlnode) {
		if (sysfs_create_link_nowarn(cxlnode->kobj, cxldev->kobj,
					cxldev->name)) {
			pr_err("failed to create link between node%d and %s\n",
					cxlnode->id, cxldev->name);
			cxl_node_free(nid);
		} else {
			list_add_tail(&cxldev->list, &cxlnode->devs);
			cxlnode->num_of_devs++;
		}
	} else {
		pr_err("failed to get cxl node\n");
	}

	cxldev->start = cxl_dax_kmem_start(dev_dax);
	cxldev->size = cxl_dax_kmem_size(dev_dax);

	set_bit(nid, cxl_devs.active);
}

static void pre_dev_dax_kmem_remove(struct dev_dax *dev_dax)
{
	struct cxl_device *cxldev = dev_dax_to_cxldev(dev_dax);
	int nid = cxldev ? cxldev->nid : dev_dax->target_node;
	struct cxl_node *cxlnode = cxldev ? cxl_node_find(cxldev->tid) : NULL;

	if (cxlnode) {
		sysfs_remove_link(cxlnode->kobj, cxldev->name);

		list_del(&cxldev->list);
		cxlnode->num_of_devs--;
		cxl_node_free(cxldev->tid);
	}

	mutex_lock(&off_on_lock);
	cxl_dev_dax_kmem_offline_and_remove(cxldev->dev_dax);
	cxl_dev_dax_kmem_add(cxldev->dev_dax);

	cxldev->tid = NUMA_NO_NODE;
	cxldev->dev_dax->target_node = nid;
	cxldev->dev_dax->region->target_node = nid;
	mutex_unlock(&off_on_lock);
}

static void post_dev_dax_kmem_remove(struct dev_dax *dev_dax)
{
	struct cxl_device *cxldev = dev_dax_to_cxldev(dev_dax);
	int nid = cxldev ? cxldev->nid : dev_dax->target_node;

	clear_bit(nid, cxl_devs.active);
}

static int __init cxl_metadata_init(void)
{
	int ret;

	cxl_kobj = kobject_create_and_add("cxl", kernel_kobj);
	if (!cxl_kobj)
		return -ENOMEM;

	cxl_devices_kobj = kobject_create_and_add("devices", cxl_kobj);
	if (!cxl_devices_kobj) {
		ret = -ENOMEM;
		goto err_cxl_devices;
	}

	cxl_nodes_kobj = kobject_create_and_add("nodes", cxl_kobj);
	if (!cxl_nodes_kobj) {
		ret = -ENOMEM;
		goto err_cxl_nodes;
	}

	return 0;

err_cxl_nodes:
	KOBJ_RELEASE(cxl_devices_kobj);

err_cxl_devices:
	KOBJ_RELEASE(cxl_kobj);

	return ret;
}

static void __exit cxl_metadata_exit(void)
{
	struct cxl_node *cxlnode, *tnode;
	struct cxl_device *cxldev, *tdev;
	int p;

	list_for_each_entry_safe(cxlnode, tnode, &cxl_node_list, entry) {
		list_for_each_entry_safe(cxldev, tdev, &cxlnode->devs, list) {
			sysfs_remove_link(cxlnode->kobj, cxldev->name);
			list_del(&cxldev->list);
			cxlnode->num_of_devs--;

			mutex_lock(&off_on_lock);
			cxl_dev_dax_kmem_offline_and_remove(cxldev->dev_dax);

			cxldev->tid = NUMA_NO_NODE;
			cxldev->dev_dax->target_node = cxldev->nid;
			cxldev->dev_dax->region->target_node = cxldev->nid;

			cxl_dev_dax_kmem_add(cxldev->dev_dax);
			mutex_unlock(&off_on_lock);
		}

		cxl_node_free(cxlnode->id);
	}

	for_each_set_bit(p, cxl_devs.probe, MAX_NUMNODES) {
		cxldev = &cxl_devs.device[p];
		if (cxldev->kobj) {
			sysfs_remove_link(cxldev->kobj,
					dev_name(&cxldev->dev_dax->dev));
			for (int i = 0; i < cxldev->num_mds; i++) {
				char mdname[CXL_MEM_DEV_NAME_MAX];
				scnprintf(mdname, sizeof(mdname), "mem%d", cxldev->mdid[i]);
				sysfs_remove_link(cxldev->kobj, mdname);
			}
			KOBJ_RELEASE(cxldev->kobj);
			clear_bit(p, cxl_devs.probe);
			cxl_devs.num_of_devs--;
		}

		if (cxl_devs.num_of_devs == 0)
			break;
	}

	KOBJ_RELEASE(cxl_nodes_kobj);
	KOBJ_RELEASE(cxl_devices_kobj);
	KOBJ_RELEASE(cxl_kobj);
}

/* from kmem.c */
static int dev_dax_kmem_probe(struct dev_dax *dev_dax)
{
	struct device *dev = &dev_dax->dev;
	unsigned long total_len = 0;
	struct dax_kmem_data *data;
	int i, rc, mapped = 0;
	int numa_node;

	pre_dev_dax_kmem_probe(dev_dax);

	/*
	 * Ensure good NUMA information for the persistent memory.
	 * Without this check, there is a risk that slow memory
	 * could be mixed in a node with faster memory, causing
	 * unavoidable performance issues.
	 */
	numa_node = dev_dax->target_node;
	if (numa_node < 0) {
		dev_warn(dev, "rejecting DAX region with invalid node: %d\n",
				numa_node);
		return -EINVAL;
	}

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc) {
			dev_info(dev, "mapping%d: %#llx-%#llx too small after alignment\n",
					i, range.start, range.end);
			continue;
		}
		total_len += range_len(&range);
	}

	if (!total_len) {
		dev_warn(dev, "rejecting DAX region without any memory after alignment\n");
		return -EINVAL;
	}

	init_node_memory_type(numa_node, dax_slowmem_type);

	rc = -ENOMEM;
	data = kzalloc(struct_size(data, res, dev_dax->nr_range), GFP_KERNEL);
	if (!data)
		goto err_dax_kmem_data;

	data->res_name = kstrdup(dev_name(dev), GFP_KERNEL);
	if (!data->res_name)
		goto err_res_name;

	rc = memory_group_register_static(numa_node, PFN_UP(total_len));
	if (rc < 0)
		goto err_reg_mgid;
	data->mgid = rc;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct resource *res;
		struct range range;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc)
			continue;

		/* Region is permanently reserved if hotremove fails. */
		res = request_mem_region(range.start, range_len(&range), data->res_name);
		if (!res) {
			dev_warn(dev, "mapping%d: %#llx-%#llx could not reserve region\n",
					i, range.start, range.end);
			/*
			 * Once some memory has been onlined we can't
			 * assume that it can be un-onlined safely.
			 */
			if (mapped)
				continue;
			rc = -EBUSY;
			goto err_request_mem;
		}
		data->res[i] = res;

		/*
		 * Set flags appropriate for System RAM.  Leave ..._BUSY clear
		 * so that add_memory() can add a child resource.  Do not
		 * inherit flags from the parent since it may set new flags
		 * unknown to us that will break add_memory() below.
		 */
		res->flags = IORESOURCE_SYSTEM_RAM;

		rc = add_subzone(numa_node, range.start, range.end);
		if (rc) {
			dev_warn(dev, "mapping%d: %#llx-%#llx subzone add failed\n",
					i, range.start, range.end);
			remove_resource(res);
			kfree(res);
			data->res[i] = NULL;
			if (mapped)
				continue;
			goto err_request_mem;
		}

		/*
		 * Ensure that future kexec'd kernels will not treat
		 * this as RAM automatically.
		 */
		rc = add_memory_driver_managed(data->mgid, range.start,
				range_len(&range), kmem_name, MHP_NID_IS_MGID);

		if (rc) {
			dev_warn(dev, "mapping%d: %#llx-%#llx memory add failed\n",
					i, range.start, range.end);
			remove_subzone(numa_node, range.start, range.end);
			remove_resource(res);
			kfree(res);
			data->res[i] = NULL;
			if (mapped)
				continue;
			goto err_request_mem;
		}

		if (numa_node != NUMA_NO_NODE)
			online_movable_memory(range.start, range_len(&range));

		mapped++;
	}

	dev_set_drvdata(dev, data);

	post_dev_dax_kmem_probe(dev_dax);

	return 0;

err_request_mem:
	memory_group_unregister(data->mgid);
err_reg_mgid:
	kfree(data->res_name);
err_res_name:
	kfree(data);
err_dax_kmem_data:
	clear_node_memory_type(numa_node, dax_slowmem_type);
	return rc;
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static void dev_dax_kmem_remove(struct dev_dax *dev_dax)
{
	int i, success = 0;
	int node = dev_dax->target_node;
	struct device *dev = &dev_dax->dev;
	struct dax_kmem_data *data = dev_get_drvdata(dev);

	pre_dev_dax_kmem_remove(dev_dax);

	/*
	 * We have one shot for removing memory, if some memory blocks were not
	 * offline prior to calling this function remove_memory() will fail, and
	 * there is no way to hotremove this memory until reboot because device
	 * unbind will succeed even if we return failure.
	 */
	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range range;
		int rc;

		rc = dax_kmem_range(dev_dax, i, &range);
		if (rc)
			continue;

		rc = remove_memory(range.start, range_len(&range));
		if (rc == 0) {
			rc = remove_subzone(node, range.start, range.end);
			if (rc)
				dev_warn(dev, "mapping%d: %#llx-%#llx remove subzone failed\n",
						i, range.start, range.end);
			remove_resource(data->res[i]);
			kfree(data->res[i]);
			data->res[i] = NULL;
			success++;
			continue;
		}
		any_hotremove_failed = true;
		dev_err(dev,
			"mapping%d: %#llx-%#llx cannot be hotremoved until the next reboot\n",
				i, range.start, range.end);
	}

	if (success >= dev_dax->nr_range) {
		memory_group_unregister(data->mgid);
		kfree(data->res_name);
		kfree(data);
		dev_set_drvdata(dev, NULL);
		/*
		 * Clear the memtype association on successful unplug.
		 * If not, we have memory blocks left which can be
		 * offlined/onlined later. We need to keep memory_dev_type
		 * for that. This implies this reference will be around
		 * till next reboot.
		 */
		clear_node_memory_type(node, dax_slowmem_type);
	}

	post_dev_dax_kmem_remove(dev_dax);
}
#else
static void dev_dax_kmem_remove(struct dev_dax *dev_dax)
{
	/*
	 * Without hotremove purposely leak the request_mem_region() for the
	 * device-dax range and return '0' to ->remove() attempts. The removal
	 * of the device from the driver always succeeds, but the region is
	 * permanently pinned as reserved by the unreleased
	 * request_mem_region().
	 */
	any_hotremove_failed = true;
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

static struct dax_device_driver device_dax_kmem_driver = {
	.probe = dev_dax_kmem_probe,
	.remove = dev_dax_kmem_remove,
	.type = DAXDRV_KMEM_TYPE,
};

static int __init dax_kmem_init(void)
{
	int rc;

	rc = cxl_metadata_init();
	if (rc)
		pr_warn("failed to init cxl metadata\n");

	rc = tierd_init();
	if (rc)
		pr_warn("failed to init tierd\n");

	/* Resource name is permanently allocated if any hotremove fails. */
	kmem_name = kstrdup_const("System RAM (kmem)", GFP_KERNEL);
	if (!kmem_name)
		return -ENOMEM;

	dax_slowmem_type = alloc_memory_type(MEMTIER_DEFAULT_DAX_ADISTANCE);
	if (IS_ERR(dax_slowmem_type)) {
		rc = PTR_ERR(dax_slowmem_type);
		goto err_dax_slowmem_type;
	}

	rc = dax_driver_register(&device_dax_kmem_driver);
	if (rc)
		goto error_dax_driver;

	return rc;

error_dax_driver:
	put_memory_type(dax_slowmem_type);
err_dax_slowmem_type:
	kfree_const(kmem_name);
	return rc;
}

static void __exit dax_kmem_exit(void)
{
	tierd_exit();
	dax_driver_unregister(&device_dax_kmem_driver);
	if (!any_hotremove_failed)
		kfree_const(kmem_name);
	cxl_metadata_exit();
	put_memory_type(dax_slowmem_type);
}

MODULE_LICENSE("GPL v2");
module_init(dax_kmem_init);
module_exit(dax_kmem_exit);
MODULE_ALIAS_DAX_DEVICE(0);
MODULE_IMPORT_NS(CXL);
