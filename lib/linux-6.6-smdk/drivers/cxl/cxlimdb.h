/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#ifndef __CXL_IMDB_H__
#define __CXL_IMDB_H__

#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include "cxlmem.h"
#include "cxl.h"

#define IMDB_CSR_RESNO 4
#define IMDB_CSR_OFFSET 0x30000
#define IMDB_CSR_SIZE 0x2000
#define IMDB_DEVICE_ID 0x3300

struct cxl_imdb {
	struct device dev;
	struct cdev cdev;
	struct resource *csr_regs;
	int id;
};

/* DEVICE_ID for SLS and IMDB are the same.
 * So to differentiate SLS and IMDB device some hack is used:
 * BAR4 register from PCI header is used for this:
 * IMDB PCI BAR4 != 0, and  SLS BAR4 == 0.
 */
static inline bool cxl_is_imdb(struct pci_dev *pdev)
{
	const int bar4 = 4;

	if (pdev->vendor == PCI_VENDOR_ID_SAMSUNG &&
	    pdev->device == IMDB_DEVICE_ID &&
	    pci_resource_start(pdev, bar4) != 0) {
		return true;
	}

	return false;
}

int cxl_add_imdb(struct device *parent, int id);

#endif //__CXL_IMDB_H__
