// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include <cxlimdb.h>
#include "core.h"

#define CXL_PNM_MAX_DEVS 65535

static dev_t cxl_pnm_major;

static int cxl_imdb_open(struct inode *inode, struct file *file)
{
	struct cxl_imdb *imdb = container_of(inode->i_cdev, struct cxl_imdb, cdev);

	file->private_data = imdb;

	dev_dbg(&imdb->dev, "Open device");

	get_device(&imdb->dev);

	return 0;
}

static int cxl_imdb_close(struct inode *inode, struct file *file)
{
	struct cxl_imdb *imdb = file->private_data;

	dev_dbg(&imdb->dev, "Close device");

	put_device(&imdb->dev);

	return 0;
}

static int __cxl_imdb_mmap(struct cxl_imdb *imdb, struct file *filp,
			  struct vm_area_struct *vma)
{
	struct resource *reg_res = imdb->csr_regs;

	size_t mapping_size = vma->vm_end - vma->vm_start;

	if (mapping_size > IMDB_CSR_SIZE) {
		dev_err(&imdb->dev, "Range 0x%lx is too big\n",
			mapping_size);
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_device(vma->vm_page_prot);
	vma->vm_pgoff += ((reg_res->start + IMDB_CSR_OFFSET) >> PAGE_SHIFT);

	dev_info(&imdb->dev, "Device mmap Offset 0x%lx, Size 0x%lx with prot 0x%lx\n",
		 vma->vm_pgoff, mapping_size, vma->vm_page_prot.pgprot);

	if (io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       mapping_size, vma->vm_page_prot)) {
		dev_err(&imdb->dev, "Device mmap failed\n");
		return -EAGAIN;
	}
	dev_dbg(&imdb->dev, "Device mmap okay\n");
	return 0;
}

static int cxl_imdb_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct cxl_imdb *cxlmd;
	int rc = -ENXIO;

	cxlmd = filp->private_data;

	rc = __cxl_imdb_mmap(cxlmd, filp, vma);

	return rc;
}

static const struct file_operations cxl_imdb_fops = { .owner = THIS_MODULE,
						     .open = cxl_imdb_open,
						     .release = cxl_imdb_close,
						     .llseek = noop_llseek,
						     .mmap = cxl_imdb_mmap };

static char *cxl_imdb_devnode(const struct device *dev, umode_t *mode, kuid_t *uid,
			     kgid_t *gid)
{
	return kasprintf(GFP_KERNEL, "cxl/%s", dev_name(dev));
}

static void cxl_imdb_release(struct device *dev)
{
	struct cxl_imdb *imdb = container_of(dev, struct cxl_imdb, dev);

	kfree(imdb);
}

static const struct device_type cxl_imdb_type = {
	.name = "imdb",
	.devnode = cxl_imdb_devnode,
	.release = cxl_imdb_release,
};

static void cxl_imdb_unregister(void *_imdb)
{
	struct cxl_imdb *imdb = _imdb;

	cdev_device_del(&imdb->cdev, &imdb->dev);
	put_device(&imdb->dev);
}

int is_cxl_imdb(struct device *dev)
{
	return dev->type == &cxl_imdb_type;
}

int cxl_add_imdb(struct device *parent, int id)
{
	struct pci_dev *pdev;
	struct cxl_imdb *imdb;
	struct device *dev;
	struct cdev *cdev;
	int rc = 0;

	imdb = kzalloc(sizeof(*imdb), GFP_KERNEL);
	if (!imdb)
		return -ENOMEM;

	pdev = to_pci_dev(parent);

	imdb->id = id;
	imdb->csr_regs = &pdev->resource[IMDB_CSR_RESNO];

	dev = &imdb->dev;
	device_initialize(dev);

	dev->bus = &cxl_bus_type;
	dev->type = &cxl_imdb_type;
	dev->parent = parent;
	dev->devt = MKDEV(cxl_pnm_major, dev->id);

	cdev = &imdb->cdev;
	cdev_init(cdev, &cxl_imdb_fops);

	rc = dev_set_name(dev, "imdb%d", imdb->id);
	if (rc)
		goto cxl_add_imdb_free;

	rc = cdev_device_add(cdev, dev);
	if (rc)
		goto cxl_add_imdb_free;

	rc = devm_add_action_or_reset(parent, cxl_imdb_unregister, imdb);
	if (rc)
		goto cxl_add_imdb_free;

	return rc;

cxl_add_imdb_free:
	kfree(imdb);

	return rc;
}
EXPORT_SYMBOL_NS(cxl_add_imdb, CXL);

__init int cxl_imdb_init(void)
{
	dev_t devt;
	int rc;

	rc = alloc_chrdev_region(&devt, 0, CXL_PNM_MAX_DEVS, "pnm");
	if (rc)
		return rc;

	cxl_pnm_major = MAJOR(devt);

	return 0;
}

void cxl_imdb_exit(void)
{
	unregister_chrdev_region(MKDEV(cxl_pnm_major, 0), CXL_PNM_MAX_DEVS);
}
