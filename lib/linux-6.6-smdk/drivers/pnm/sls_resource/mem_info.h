/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#ifndef __MEM_INFO_H__
#define __MEM_INFO_H__

#include <linux/sls_common.h>

const struct sls_mem_info *sls_create_mem_info(void);
void sls_destroy_mem_info(void);
const struct sls_mem_info *sls_get_mem_info(void);

const struct sls_mem_cunit_info *
sls_create_mem_cunit_info(const struct sls_mem_info *mem_info);
void sls_destroy_mem_cunit_info(const struct sls_mem_cunit_info *cunit_mem_info);

enum memory_type sls_get_cache_policy(enum sls_mem_blocks_e type);
uint64_t sls_get_memory_size(void);
uint64_t sls_get_base(void);
int sls_get_align(void);

#endif /* __MEM_INFO_H__ */
