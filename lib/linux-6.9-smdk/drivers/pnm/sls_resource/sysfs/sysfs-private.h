/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#include <linux/kobject.h>
#include <linux/sls_common.h>

#define SYSFS_GROUPS_COUNT 1
#define REGION_SYSFS_ATTR_COUNT 4
#define RAW_REGION_SYSFS_ATTR_COUNT 3
#define WITH_NULL_TERM(var) (var + 1)

int sls_build_cunits_sysfs(struct kobject *parent,
			   const struct sls_mem_cunit_info *mem_cunit_info);
void sls_destroy_cunits_sysfs(void);

int sls_build_mappings_sysfs(struct kobject *parent,
			     const struct sls_mem_info *mem_info);
void sls_destroy_mappings_sysfs(void);

int sls_build_topology_sysfs(struct kobject *parent);
void sls_destroy_topology_sysfs(void);
