/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#ifndef __IMDB_ALLOCATOR_H__
#define __IMDB_ALLOCATOR_H__

#include <linux/fs.h>
#include <linux/imdb_resources.h>

int initialize_memory_allocator(void);
void destroy_memory_allocator(void);
int reset_memory_allocator(void);

int allocator_ioctl(struct file *filp, unsigned int cmd,
		    unsigned long __user arg);
int allocator_clear_res(const struct pnm_allocation *allocation);

uint64_t get_avail_size(void);
uint64_t mem_size_in_bytes(void);

uint64_t get_granularity(void);

void imdb_alloc_lock(void);
void imdb_alloc_unlock(void);

#endif
