/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022 Samsung LTD. All rights reserved. */

#ifndef __SLS_ALLOCATOR_H__
#define __SLS_ALLOCATOR_H__

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/sls_common.h>
#include <linux/sls_resources.h>
#include <linux/types.h>

int init_sls_allocator(const struct sls_mem_cunit_info *mem_cunit_info);
/*
 * Not proccess(thread)-safe.
 * `mem_info` might be NULL, if it is
 * then just reset allocator, if it's not
 * then reset with memory pools initialization.
 */
int reset_sls_allocator(void);
void cleanup_sls_allocator(void);
int mem_process_ioctl(struct file *filp, unsigned int cmd,
		      unsigned long __user arg);
int deallocate_memory_unsafe(struct pnm_allocation req);
void lock_sls_allocator(void);
void unlock_sls_allocator(void);

uint64_t get_total_size(uint8_t cunit);
uint64_t get_free_size(uint8_t cunit);

#endif /* __SLS_ALLOCATOR_H__ */
