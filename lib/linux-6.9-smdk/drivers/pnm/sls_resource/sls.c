// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#include "mem_info.h"
#include "private.h"
#include "process_manager.h"
#include "sysfs/sysfs.h"
#include "topo/params.h"

#include "device_resource.h"

#include <linux/cdev.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/pnm/log.h>
#include <linux/slab.h>
#include <linux/sls_resources.h>

#ifdef SLS_DRIVER_VERSION
MODULE_VERSION(SLS_DRIVER_VERSION);
#endif

#define SLS_RESOURCE_BASE_MINOR 0

static const struct sls_mem_cunit_info *mem_cunit_info;
static struct device *sls_resource_device;
static struct cdev sls_resource_cdev;
static dev_t sls_resource_device_number;

int sls_release(struct inode *node, struct file *filp)
{
	return release_sls_process(filp);
}

int sls_open(struct inode *inode, struct file *filp)
{
	return register_sls_process(filp);
}

static void dump_ioctl_err(unsigned int cmd)
{
	PNM_ERR(" ioctl cmd: magic %u, direction %u, size %u, number %u\n",
		_IOC_TYPE(cmd), _IOC_DIR(cmd), _IOC_SIZE(cmd), _IOC_NR(cmd));
}

long sls_ioctl(struct file *filp, unsigned int cmd, unsigned long __user arg)
{
	int retval = 0;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SLS_IOC_MAGIC) {
		PNM_ERR("Wrong ioctl magic. Aborting ioctl:\n");
		dump_ioctl_err(cmd);
		PNM_ERR(" expected magic: %u\n", SLS_IOC_MAGIC);
		return -ENOTTY;
	}
	if (_IOC_NR(cmd) > SLS_IOC_MAXNR) {
		PNM_ERR("Ioctl number too large. Aborting ioctl:\n");
		dump_ioctl_err(cmd);
		PNM_ERR(" maximum ioctl number: %u\n", SLS_IOC_MAXNR);
		return -ENOTTY;
	}

	PNM_DBG("Handling SLS ioctl %u with arg 0x%lx\n", _IOC_NR(cmd), arg);

	switch (cmd) {
	case ALLOCATE_MEMORY:
	case DEALLOCATE_MEMORY:
	case MAKE_SHARED_ALLOC:
	case GET_SHARED_ALLOC:
		retval = mem_process_ioctl(filp, cmd, arg);
		break;
	case GET_CUNIT:
	case RELEASE_CUNIT:
		retval = cunit_sched_ioctl(filp, cmd, arg);
		break;
	case NOP:
		// Do nothing. We need it only for context switch
		break;
	default:
		PNM_ERR("Unknown ioctl command:\n");
		dump_ioctl_err(cmd);
		retval = -ENOTTY;
	}

	PNM_DBG("Returning %d from ioctl\n", retval);
	return retval;
}

static const struct file_operations sls_resource_ops = {
	.owner = THIS_MODULE,
	.open = sls_open,
	.release = sls_release,
	.unlocked_ioctl = sls_ioctl,
};

static int sls_alloc_cdev_region(void)
{
	return alloc_chrdev_region(&sls_resource_device_number,
				   SLS_RESOURCE_BASE_MINOR, 1,
				   SLS_RESOURCE_DEVICE_NAME);
}

static void sls_destroy_chrdev_region(void)
{
	if (likely(sls_resource_device_number))
		unregister_chrdev_region(sls_resource_device_number, 1);
}

static int sls_init_cdev(void)
{
	cdev_init(&sls_resource_cdev, &sls_resource_ops);

	return cdev_add(&sls_resource_cdev, sls_resource_device_number, 1);
}

static void sls_destroy_cdev(void)
{
	cdev_del(&sls_resource_cdev);
}

struct device *get_sls_device(void)
{
	return sls_resource_device;
}

int init_sls_device(void)
{
	const struct sls_mem_info *mem_info;
	int err;

	PNM_INF("Initializing SLS device\n");

	err = init_topology();

	if (unlikely(err))
		return err;

	/* Alloc chrdev region */
	err = sls_alloc_cdev_region();

	if (unlikely(err)) {
		PNM_ERR("Failed to allocate chrdev region\n");
		goto alloc_chrdev_fail;
	}
	PNM_DBG("sls_resource chrdev region: major %d, minor %d\n",
		MAJOR(sls_resource_device_number),
		MINOR(sls_resource_device_number));

	/* Initialize sls_resource device */
	err = pnm_create_resource_device(SLS_RESOURCE_DEVICE_NAME,
					 &sls_resource_device_number,
					 &sls_resource_device);
	if (unlikely(err))
		goto resource_device_fail;

	err = sls_init_cdev();

	if (unlikely(err)) {
		PNM_ERR("Failed to add %s cdev\n", SLS_RESOURCE_DEVICE_NAME);
		goto init_cdev_fail;
	}

	/* Initialize device mem_info */
	mem_info = sls_create_mem_info();
	if (!mem_info)
		goto mem_info_fail;

	/* Initialize device mem_cunit_info */
	mem_cunit_info = sls_create_mem_cunit_info(mem_info);
	if (!mem_cunit_info)
		goto mem_cunit_info_fail;

	/* Initialize memory allocator */
	err = init_sls_allocator(mem_cunit_info);
	if (err)
		goto allocator_fail;

	/* Reset cunits status and synchronization primitives */
	err = init_cunit_sched();
	if (err)
		goto cunit_scheduler_fail;

	/* Build sysfs for device */
	err = sls_build_sysfs(mem_cunit_info, mem_info, sls_resource_device);
	if (unlikely(err))
		goto build_sysfs_fail;

	PNM_INF("Initialization is done");
	return 0;

build_sysfs_fail:
	destroy_cunit_sched();
	cleanup_process_manager();
cunit_scheduler_fail:
	cleanup_sls_allocator();
allocator_fail:
	sls_destroy_mem_cunit_info(mem_cunit_info);
mem_cunit_info_fail:
	sls_destroy_mem_info();
mem_info_fail:
	sls_destroy_cdev();
init_cdev_fail:
	pnm_destroy_resource_device(sls_resource_device);
resource_device_fail:
	sls_destroy_chrdev_region();
alloc_chrdev_fail:
	return err;
}

void cleanup_sls_device(void)
{
	PNM_INF("Cleaning up SLS device\n");

	/* Free allocated memory */
	cleanup_sls_allocator();

	/* Free mem_info structure */
	sls_destroy_mem_info();

	/* Free cunit_mem_info*/
	sls_destroy_mem_cunit_info(mem_cunit_info);

	/* Free allocated memory if any user processes alive upon device remove*/
	cleanup_process_manager();

	/* Reset state */
	destroy_cunit_sched();

	/* Destroy sysfs  */
	sls_destroy_sysfs(sls_resource_device);

	/* Destroy sls_resource chrdev */
	sls_destroy_cdev();

	/* Destroy sls_resource device */
	pnm_destroy_resource_device(sls_resource_device);

	/* Destroy sls_resource chrdev region */
	sls_destroy_chrdev_region();
}
