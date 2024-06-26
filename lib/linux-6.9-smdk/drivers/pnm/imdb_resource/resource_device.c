// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#include "allocator.h"
#include "private.h"
#include "proc_mgr.h"
#include "sysfs.h"
#include "thread_sched.h"
#include "topo/params.h"

#include "device_resource.h"

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/imdb_resources.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pnm/log.h>

static bool use_dax;
module_param(use_dax, bool, 0644);
MODULE_PARM_DESC(use_dax, "Enable DAX for IMDB");

static long imdb_ioctl(struct file *file, unsigned int cmd,
		       unsigned long __user arg)
{
	int result = 0;

	if (_IOC_TYPE(cmd) != IMDB_RESOURCE_IOC_MAGIC) {
		PNM_ERR("Wrong ioctl request: %d", cmd);
		return -ENOTTY;
	}

	switch (cmd) {
	case IMDB_IOCTL_ALLOCATE:
	case IMDB_IOCTL_DEALLOCATE:
	case IMDB_IOCTL_MAKE_SHARED_ALLOC:
	case IMDB_IOCTL_GET_SHARED_ALLOC:
		result = allocator_ioctl(file, cmd, arg);
		break;
	case IMDB_IOCTL_GET_THREAD:
	case IMDB_IOCTL_RELEASE_THREAD:
		result = thread_sched_ioctl(file, cmd, arg);
		break;
	default:
		result = -ENOTTY;
		PNM_ERR("Unknown ioctl: %d", cmd);
	}

	return result;
}

static int imdb_open(struct inode *inode, struct file *file)
{
	return imdb_register_process(file);
}

static int imdb_close(struct inode *inode, struct file *file)
{
	return imdb_release_process(file);
}

static const struct file_operations imdb_resource_operation = {
	.open = imdb_open,
	.release = imdb_close,
	.owner = THIS_MODULE,
	.unlocked_ioctl = imdb_ioctl,
	.llseek = noop_llseek
};
static int device_major_number = -1;
static struct cdev imdb_resource_cdev;
static struct device *imdb_resource_device;

phys_addr_t imdb_get_data_addr(void)
{
	return imdb_topo()->base_data_addr;
}
EXPORT_SYMBOL(imdb_get_data_addr);

uint64_t imdb_get_mem_size(void)
{
	return mem_size_in_bytes();
}
EXPORT_SYMBOL(imdb_get_mem_size);

static int imdb_alloc_chrdev_region(void)
{
	return alloc_chrdev_region(&device_major_number, 0, 1,
				   IMDB_RESOURCE_DEVICE_NAME);
}

static void imdb_destroy_chrdev_region(void)
{
	if (likely(device_major_number))
		unregister_chrdev_region(device_major_number, 1);
}

static int imdb_init_cdev(void)
{
	cdev_init(&imdb_resource_cdev, &imdb_resource_operation);

	return cdev_add(&imdb_resource_cdev, device_major_number, 1);
}

static void imdb_destroy_cdev(void)
{
	cdev_del(&imdb_resource_cdev);
}

static int init_imdb_resource_module(void)
{
	int result = 0;

	PNM_INF("Begin IMDB Resource Manager initialization...");

	result = imdb_alloc_chrdev_region();

	if (unlikely(result)) {
		PNM_ERR("Fail to allocate chrdev region");
		goto alloc_chrdev_fail;
	}

	result = pnm_create_resource_device(IMDB_RESOURCE_DEVICE_NAME,
					    &device_major_number,
					    &imdb_resource_device);

	if (unlikely(result)) {
		PNM_ERR("IMDB Resource Manager initialization failed");
		goto resource_device_fail;
	}

	result = imdb_init_cdev();

	if (unlikely(result)) {
		PNM_ERR("Fail to add %s cdev.", IMDB_RESOURCE_DEVICE_NAME);
		goto init_cdev_fail;
	}

	result = initialize_memory_allocator();

	if (unlikely(result)) {
		PNM_ERR("Fail to initialize memory allocator.");
		goto allocator_fail;
	}

	result = init_thread_sched();

	if (unlikely(result)) {
		PNM_ERR("Fail to initialize memory threads scheduler.");
		goto thread_sched_fail;
	}

	result = imdb_build_sysfs(imdb_resource_device);

	if (unlikely(result)) {
		PNM_ERR("Fail to build sysfs.");
		goto build_sysfs_fail;
	}

	PNM_INF("Initialization is done");
	return 0;

build_sysfs_fail:
	destroy_thread_sched();
thread_sched_fail:
	destroy_memory_allocator();
allocator_fail:
	imdb_destroy_cdev();
init_cdev_fail:
	pnm_destroy_resource_device(imdb_resource_device);
resource_device_fail:
	imdb_destroy_chrdev_region();
alloc_chrdev_fail:
	return result;
}

static void exit_imdb_resource_module(void)
{
	imdb_destroy_sysfs(imdb_resource_device);

	imdb_destroy_cdev();

	pnm_destroy_resource_device(imdb_resource_device);

	imdb_destroy_chrdev_region();

	destroy_memory_allocator();

	destroy_thread_sched();

	imdb_destroy_proc_manager();

	PNM_INF("IMDB Resource Manager unloaded.");
}

// imdb initialization is called earlier than this check.
// so we can be sure that if use_dax is set, dax is initialized.
bool imdb_is_dax_enabled(void)
{
	return use_dax;
}
EXPORT_SYMBOL(imdb_is_dax_enabled);

static int __init init_mod(void)
{
	int err = init_imdb_resource_module();

	if (err)
		return err;

	if (use_dax)
		err = init_imdb_dax_dev(imdb_resource_device,
					PNM_DAX_MINOR_ID); // dax0.0
	else
		PNM_INF("DAX is disabled: use_dax=off\n");

	return err;
}

static void __exit exit_mod(void)
{
	exit_imdb_resource_module();
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IMDB Resource Manager");

module_init(init_mod);
module_exit(exit_mod);
