// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include <linux/pnm/alloc.h>
#include <linux/pnm/log.h>
#include <linux/range.h>
#include <linux/slab.h>

void pnm_alloc_lock(struct pnm_alloc *alloc)
{
	mutex_lock(&alloc->lock);
}
EXPORT_SYMBOL(pnm_alloc_lock);

void pnm_alloc_unlock(struct pnm_alloc *alloc)
{
	mutex_unlock(&alloc->lock);
}
EXPORT_SYMBOL(pnm_alloc_unlock);

void pnm_alloc_reset_lock(struct pnm_alloc *alloc)
{
	if (unlikely(mutex_is_locked(&alloc->lock))) {
		PNM_ERR("Mutex unlock forced.\n");
		/*
		 * [TODO: @p.bred] Question about UB exclusion, 2 variants:
		 * 1) current, force unlock if there is any lockage, it can
		 * happen, for example, if we forgot to unlock. But potentially
		 * there is also a chance - that this is the real allocator work
		 * somewhere else, then an UB is possible. The advantage of this
		 * method is also the complete reinitialization of the mutex.
		 * 2) a mutex is never reinitialized, it's created only
		 * statically, and is directly used in reinitialization methods
		 * with lock/unlock. Thus, we wait in any case for the
		 * completion of any exist locking. However, in this case,
		 * we can wait forever, and also if we try to reinitialize
		 * the mutex, there is a high chance of initializing a locked
		 * mutex, which is an UB.
		 * -----
		 * There may be a more reliable solution to this problem,
		 * or a combination of both approaches.
		 */
		pnm_alloc_unlock(alloc);
	}
}
EXPORT_SYMBOL(pnm_alloc_reset_lock);

static int set_granularity(struct pnm_alloc_properties *props, uint64_t gran)
{
	if (unlikely(!is_power_of_2(gran))) {
		PNM_ERR("Memory granularity should be a power of 2!\n");
		return -EINVAL;
	}
	props->gran = gran;
	return 0;
}

static int init_pool(struct pnm_alloc_properties *props, uint8_t idx)
{
	struct range range = props->ranges[idx];
	struct gen_pool *pool =
		gen_pool_create(ilog2(props->gran), NUMA_NO_NODE);
	int err_code;
	uint64_t start = range.start, sz = range_len(&props->ranges[idx]);

	if (unlikely(!pool)) {
		PNM_ERR("gen_pool_create failed for pool[%hhu], granularity = [%llu]\n",
			idx, props->gran);
		return -ENOMEM;
	}

	/*
	 * Reserve granularity size from start if it's zero to eliminate
	 * allocator ambiguity, technically we could back off by just 1 byte,
	 * but given the alignment, the granularity is preferred.
	 * Otherwise, the allocator may return 0 as a pointer to the real
	 * allocation, and we'll not be able to distinguish the result from
	 * the allocator fail.
	 */
	if (!start)
		start += props->gran;

	err_code = gen_pool_add(pool, start, sz, NUMA_NO_NODE);
	if (unlikely(err_code < 0)) {
		PNM_ERR("Failed to init pool[%hhu], start=[%llu], size=[%llu] with error [%d]\n",
			idx, start, sz, err_code);
		gen_pool_destroy(pool);
		return err_code;
	}

	PNM_INF("Memory pool[%hhu] initialized: granularity=[%llu], start=[%llu], size=[%llu]\n",
		idx, props->gran, start, sz);
	props->pools[idx] = pool;

	return 0;
}

int pnm_alloc_pool_malloc(struct pnm_alloc *alloc, uint8_t idx, uint64_t sz,
			  uint64_t *addr)
{
	struct pnm_alloc_properties *props = &alloc->props;

	if (idx >= props->nr_pools) {
		PNM_ERR("Pool[%hhu] doesn't exist.\n", idx);
		return -EINVAL;
	}

	if (!sz) {
		PNM_ERR("Trying to allocate zero memory.\n");
		return -EINVAL;
	}

	if (props->aligned)
		sz = ALIGN(sz, props->gran);

	*addr = gen_pool_alloc(props->pools[idx], sz);

	if (unlikely(!*addr)) {
		PNM_ERR("Pool[%hhu]: no free memory for object with size = [%llu]\n",
			idx, sz);
		return -ENOMEM;
	}

	/* Align if reserved indent there is before, see init_pool func */
	if (!props->ranges[idx].start)
		*addr -= props->gran;

	return 0;
}
EXPORT_SYMBOL(pnm_alloc_pool_malloc);

int pnm_alloc_pool_free(struct pnm_alloc *alloc, uint8_t idx, uint64_t sz,
			uint64_t *addr)
{
	struct pnm_alloc_properties *props = &alloc->props;

	if (idx >= props->nr_pools) {
		PNM_ERR("Pool[%hhu] doesn't exist.\n", idx);
		return -EINVAL;
	}

	/* Align if reserved indent there is before, see init_pool func */
	if (!props->ranges[idx].start)
		*addr += props->gran;

	if (props->aligned)
		sz = ALIGN(sz, props->gran);

	/*
	 * [TODO: @e-kutovoi] This check doesn't save against pointers
	 * inside of allocated memory
	 * i.e.:
	 *   addr = alloc(0x100)
	 *   ...
	 *   dealloc(addr + 4)
	 * We should consider either adding additional bookkeeping of
	 * valid allocations in here, or calling into process manager
	 * before this call.
	 */
	if (unlikely(!gen_pool_has_addr(props->pools[idx], *addr, sz))) {
		PNM_ERR("The object at %llu of size %llu from pool[%u] doesn't exist\n",
			*addr, sz, idx);
		return -EINVAL;
	}

	gen_pool_free(props->pools[idx], *addr, sz);

	return 0;
}
EXPORT_SYMBOL(pnm_alloc_pool_free);

/* Make each pool chunk size == 0 */
static void mem_pool_mark_zero_chunk_size(struct gen_pool *pool,
					  struct gen_pool_chunk *chunk,
					  void *data)
{
	chunk->end_addr = chunk->start_addr - 1;
}

static void cleanup_pools(struct pnm_alloc_properties *props)
{
	uint8_t idx;
	struct gen_pool *pool;

	for (idx = 0; idx < props->nr_pools; ++idx) {
		pool = props->pools[idx];
		if (unlikely(!pool)) {
			PNM_WRN("Trying to cleanup memory pool[%hhu] that was not created\n",
				idx);
			continue;
		}
		if (unlikely(gen_pool_avail(pool) != gen_pool_size(pool))) {
			PNM_ERR("Pool[%hhu]: non-deallocated objects, size: %zu, avail: %zu\n",
				idx, gen_pool_size(pool), gen_pool_avail(pool));
			/*
			 * To destroy memory pool gen_pool_destroy checks
			 * outstanding allocations up to the kernel panic.
			 * Mark all our memory chunks with zero size for
			 * forced deallocation ignoring that.
			 */
			gen_pool_for_each_chunk(
				pool, mem_pool_mark_zero_chunk_size, NULL);
		}
		gen_pool_destroy(pool);
		PNM_INF("Memory pool[%hhu] is destroyed\n", idx);
	}
	kfree(props->pools);
}

static int init_pools(struct pnm_alloc_properties *props)
{
	uint8_t idx;
	int err_code;

	props->pools =
		kcalloc(props->nr_pools, sizeof(props->pools[0]), GFP_KERNEL);
	if (!props->pools)
		return -ENOMEM;

	for (idx = 0; idx < props->nr_pools; ++idx) {
		err_code = init_pool(props, idx);
		if (unlikely(err_code)) {
			cleanup_pools(props);
			return err_code;
		}
	}

	return 0;
}

static int init_ranges(struct pnm_alloc_properties *props, struct range *ranges)
{
	uint8_t idx;

	props->ranges =
		kcalloc(props->nr_pools, sizeof(props->ranges[0]), GFP_KERNEL);
	if (!props->ranges)
		return -ENOMEM;

	for (idx = 0; idx < props->nr_pools; ++idx)
		props->ranges[idx] = ranges[idx];

	return 0;
}

static void cleanup_ranges(struct pnm_alloc_properties *props)
{
	kfree(props->ranges);
}

int pnm_alloc_init(struct pnm_alloc *alloc, uint64_t gran, uint8_t nr_pools,
		   struct range *ranges)
{
	struct pnm_alloc_properties *props = &alloc->props;
	int err = set_granularity(props, gran);

	if (err)
		return err;

	props->aligned = false;
	mutex_init(&alloc->lock);
	props->nr_pools = nr_pools;
	err = init_ranges(props, ranges);
	return err ? err : init_pools(props);
}
EXPORT_SYMBOL(pnm_alloc_init);

int pnm_alloc_reset(struct pnm_alloc *alloc)
{
	pnm_alloc_reset_lock(alloc);
	cleanup_pools(&alloc->props);
	return init_pools(&alloc->props);
}
EXPORT_SYMBOL(pnm_alloc_reset);

void pnm_alloc_cleanup(struct pnm_alloc *alloc)
{
	pnm_alloc_reset_lock(alloc);
	cleanup_pools(&alloc->props);
	cleanup_ranges(&alloc->props);
	mutex_destroy(&alloc->lock);
}
EXPORT_SYMBOL(pnm_alloc_cleanup);

uint8_t pnm_alloc_most_free_pool(struct pnm_alloc *alloc)
{
	uint8_t idx, res_idx = 0;
	struct pnm_alloc_properties *props = &alloc->props;
	uint64_t pool_free_space,
		max_free_space = gen_pool_avail(props->pools[0]);

	for (idx = 1; idx < props->nr_pools; ++idx) {
		pool_free_space = gen_pool_avail(props->pools[idx]);
		if (max_free_space < pool_free_space) {
			max_free_space = pool_free_space;
			res_idx = idx;
		}
	}
	return res_idx;
}
EXPORT_SYMBOL(pnm_alloc_most_free_pool);

uint64_t pnm_alloc_total_sz_pool(struct pnm_alloc *alloc, uint8_t idx)
{
	uint64_t sz = 0;
	struct pnm_alloc_properties *props = &alloc->props;

	pnm_alloc_lock(alloc);
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(*props);
		sz = gen_pool_size(props->pools[idx]);
	}
	pnm_alloc_unlock(alloc);

	return sz;
}
EXPORT_SYMBOL(pnm_alloc_total_sz_pool);

uint64_t pnm_alloc_total_sz(struct pnm_alloc *alloc)
{
	uint64_t sz = 0;
	struct pnm_alloc_properties *props = &alloc->props;
	uint8_t idx;

	pnm_alloc_lock(alloc);
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(*props);
		for (idx = 0; idx < props->nr_pools; ++idx)
			sz += gen_pool_size(props->pools[idx]);
	}
	pnm_alloc_unlock(alloc);

	return sz;
}
EXPORT_SYMBOL(pnm_alloc_total_sz);

uint64_t pnm_alloc_free_sz_pool(struct pnm_alloc *alloc, uint8_t idx)
{
	uint64_t sz = 0;
	struct pnm_alloc_properties *props = &alloc->props;

	pnm_alloc_lock(alloc);
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(*props);
		sz = gen_pool_avail(props->pools[idx]);
	}
	pnm_alloc_unlock(alloc);

	return sz;
}
EXPORT_SYMBOL(pnm_alloc_free_sz_pool);

uint64_t pnm_alloc_free_sz(struct pnm_alloc *alloc)
{
	uint64_t sz = 0;
	struct pnm_alloc_properties *props = &alloc->props;
	uint8_t idx;

	pnm_alloc_lock(alloc);
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(*props);
		for (idx = 0; idx < props->nr_pools; ++idx)
			sz += gen_pool_avail(props->pools[idx]);
	}
	pnm_alloc_unlock(alloc);

	return sz;
}
EXPORT_SYMBOL(pnm_alloc_free_sz);

uint64_t pnm_alloc_granularity(struct pnm_alloc *alloc)
{
	uint64_t gran;

	pnm_alloc_lock(alloc);
	gran = alloc->props.gran;
	pnm_alloc_unlock(alloc);

	return gran;
}
EXPORT_SYMBOL(pnm_alloc_granularity);

void pnm_alloc_set_force_aligned(struct pnm_alloc *alloc, bool alloc_aligned)
{
	alloc->props.aligned = alloc_aligned;
}
EXPORT_SYMBOL(pnm_alloc_set_force_aligned);

bool pnm_alloc_get_force_aligned(struct pnm_alloc *alloc)
{
	return alloc->props.aligned;
}
EXPORT_SYMBOL(pnm_alloc_get_force_aligned);
