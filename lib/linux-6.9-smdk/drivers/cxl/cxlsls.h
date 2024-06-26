/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#ifndef __CXL_SLS_H__
#define __CXL_SLS_H__

#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#define PNM_SLS_DEVICE_ID 0x3300

#if IS_ENABLED(CONFIG_DEV_SLS_CXL_BUS_DRIVER)
int cxl_add_sls(struct device *parent, int id);
bool is_cxl_sls(struct device *dev);

/* DEVICE_ID for SLS and IMDB are the same.
 * So to differentiate SLS and IMDB device some hack is used:
 * BAR4 register from PCI header is used for this:
 * IMDB PCI BAR4 != 0, and  SLS BAR4 == 0.
 */
static inline bool cxl_is_sls(struct pci_dev *pdev)
{
	const int bar4 = 4;

	if (pdev->vendor == PCI_VENDOR_ID_SAMSUNG &&
	    pdev->device == PNM_SLS_DEVICE_ID &&
	    pci_resource_start(pdev, bar4) == 0) {
		return true;
	}

	return false;
}
#else // CONFIG_DEV_SLS_CXL_BUS_DRIVER is disabled
static inline int cxl_add_sls(struct device *parent, int id)
{
	return -ENXIO;
}

static inline bool is_cxl_sls(struct device *dev)
{
	return false;
}

static inline bool cxl_is_sls(struct pci_dev *pdev)
{
	return false;
}
#endif // CONFIG_DEV_SLS_CXL_BUS_DRIVER

#endif // __CXL_SLS_H__
