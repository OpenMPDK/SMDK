/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#ifndef __SLS_SYSFS__
#define __SLS_SYSFS__

#include <linux/device.h>
#include <linux/sls_common.h>

int sls_build_sysfs(const struct sls_mem_cunit_info *mem_cunit_info,
		    const struct sls_mem_info *mem_info,
		    struct device *resource_dev);
void sls_destroy_sysfs(struct device *dev);

#endif /* __SLS_SYSFS__ */
