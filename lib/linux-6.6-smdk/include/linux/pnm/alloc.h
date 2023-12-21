/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __PNM_ALLOCATOR_H__
#define __PNM_ALLOCATOR_H__

#include <linux/genalloc.h>
#include <linux/mutex.h>
#include <linux/range.h>
#include <linux/types.h>

struct pnm_alloc {
	struct pnm_alloc_properties {
		/* Allocator granularity in bytes, the number should be a power of 2 */
		uint64_t gran;
		/* Should we align size relative to granularity */
		bool aligned;
		uint8_t nr_pools;
		/* GenAlloc memory pools, each pool has it's own virtual
		 * range, currently this range is not bound to the real memory by PA,
		 * Pool range: [reserved_indent, pool_size - reserved_indent]
		 */
		struct gen_pool **pools;
		/* Raw VA ranges for each pool */
		struct range *ranges;
	} props;
	struct mutex lock;
};

void pnm_alloc_lock(struct pnm_alloc *alloc);
void pnm_alloc_unlock(struct pnm_alloc *alloc);
void pnm_alloc_reset_lock(struct pnm_alloc *alloc);

int pnm_alloc_init(struct pnm_alloc *alloc, uint64_t gran, uint8_t nr_pools,
		   struct range *ranges);
int pnm_alloc_reset(struct pnm_alloc *alloc);
void pnm_alloc_cleanup(struct pnm_alloc *alloc);

int pnm_alloc_pool_malloc(struct pnm_alloc *alloc, uint8_t idx, uint64_t sz,
			  uint64_t *addr);
int pnm_alloc_pool_free(struct pnm_alloc *alloc, uint8_t idx, uint64_t sz,
			uint64_t *addr);

uint64_t pnm_alloc_total_sz(struct pnm_alloc *alloc);
uint64_t pnm_alloc_free_sz(struct pnm_alloc *alloc);

uint8_t pnm_alloc_most_free_pool(struct pnm_alloc *alloc);
uint64_t pnm_alloc_total_sz_pool(struct pnm_alloc *alloc, uint8_t idx);
uint64_t pnm_alloc_free_sz_pool(struct pnm_alloc *alloc, uint8_t idx);

uint64_t pnm_alloc_granularity(struct pnm_alloc *alloc);
void pnm_alloc_set_force_aligned(struct pnm_alloc *alloc, bool alloc_aligned);
bool pnm_alloc_get_force_aligned(struct pnm_alloc *alloc);

#endif /* __PNM_ALLOCATOR_H__ */
