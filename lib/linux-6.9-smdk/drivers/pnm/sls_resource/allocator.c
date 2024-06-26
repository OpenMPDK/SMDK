// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#include "allocator.h"
#include "private.h"
#include "process_manager.h"
#include "topo/params.h"

#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/numa.h>
#include <linux/pnm/alloc.h>
#include <linux/pnm/log.h>
#include <linux/pnm/shared_pool.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static const struct sls_mem_cunit_info *mem_cunit_info;
static struct pnm_alloc alloc;
static struct pnm_shared_pool sls_shared_pool;

void lock_sls_allocator(void)
{
	pnm_alloc_lock(&alloc);
}

void unlock_sls_allocator(void)
{
	pnm_alloc_unlock(&alloc);
}

static uint64_t get_cunit_size(uint8_t cunit)
{
	const uint64_t nr_cunits = mem_cunit_info->nr_cunits;
	const uint64_t nr_regions_per_cunit =
		mem_cunit_info->nr_regions / nr_cunits;
	const struct sls_mem_cunit_region *cunit_regions =
		&mem_cunit_info->regions[cunit * nr_regions_per_cunit];
	uint64_t idx;

	for (idx = 0; idx < nr_regions_per_cunit; ++idx)
		if (cunit_regions[idx].type == SLS_BLOCK_BASE)
			return range_len(&cunit_regions[idx].range);

	PNM_ERR("There is no BASE region for cunit[%hhu]\n", cunit);
	return 0;
}

int init_sls_allocator(const struct sls_mem_cunit_info *cunit_info)
{
	uint64_t gran = sls_topo()->alignment_sz;
	uint8_t idx, nr_cunits = cunit_info->nr_cunits;
	struct range *ranges =
		kcalloc(nr_cunits, sizeof(struct range), GFP_KERNEL);
	int err_code;

	PNM_DBG("Initializing SLS allocator\n");
	if (!cunit_info) {
		PNM_ERR("cunit_info is NULL!\n");
		return -EINVAL;
	}
	mem_cunit_info = cunit_info;

	if (!ranges)
		return -ENOMEM;
	for (idx = 0; idx < nr_cunits; ++idx)
		ranges[idx] = (struct range){ 0, get_cunit_size(idx) - 1 };

	err_code = pnm_alloc_init(&alloc, gran, nr_cunits, ranges);
	kfree(ranges);

	err_code = pnm_shared_pool_init(&sls_shared_pool);
	if (err_code)
		pnm_alloc_cleanup(&alloc);

	return err_code;
}

int reset_sls_allocator(void)
{
	int err = 0;
	PNM_DBG("Resetting SLS allocator\n");
	err = pnm_shared_pool_reset(&sls_shared_pool);
	if (unlikely(err))
		return err;
	return pnm_alloc_reset(&alloc);
}

void cleanup_sls_allocator(void)
{
	PNM_DBG("Cleaning up SLS allocator\n");
	pnm_shared_pool_cleanup(&sls_shared_pool);
	pnm_alloc_cleanup(&alloc);
}

static int allocate_memory_ioctl(struct file *filp, struct pnm_allocation *kreq)
{
	int err = 0;

	lock_sls_allocator();
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(alloc.props);

		if (kreq->memory_pool == SLS_ALLOC_ANY_CUNIT)
			kreq->memory_pool = pnm_alloc_most_free_pool(&alloc);

		err = pnm_alloc_pool_malloc(&alloc, kreq->memory_pool,
					    kreq->size, &kreq->addr);

		if (unlikely(err)) {
			goto unlock_and_exit;
		}

		err = sls_proc_register_alloc(filp, *kreq);

		if (!err) {
			PNM_DBG("Allocated obj: pool[%u], cunit_offset = [0x%llx], size = [%llu]\n",
				kreq->memory_pool, kreq->addr, kreq->size);
		}
	}

unlock_and_exit:
	unlock_sls_allocator();

	return err;
}

int deallocate_memory_unsafe(struct pnm_allocation req)
{
	if (req.is_global == 1) {
		int rc = 0;

		rc = pnm_shared_pool_remove_alloc(&sls_shared_pool, &req);
		if (rc)
			return rc;
	}

	return pnm_alloc_pool_free(&alloc, req.memory_pool, req.size,
				   &req.addr);
}

static int deallocate_memory_ioctl(struct file *filp,
				   struct pnm_allocation *kreq)
{
	int err = 0;

	lock_sls_allocator();
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(alloc.props);

		err = sls_proc_remove_alloc(filp, *kreq);

		if (!err)
			err = deallocate_memory_unsafe(*kreq);
	}
	unlock_sls_allocator();

	return err;
}

static int get_shared_alloc_ioctl(struct file *filp,
				  struct pnm_allocation *kreq)
{
	int err = 0;

	lock_sls_allocator();
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(alloc.props);
		err = pnm_shared_pool_get_alloc(&sls_shared_pool, kreq);
		if (likely(!err))
			err = sls_proc_register_alloc(filp, *kreq);
	}
	unlock_sls_allocator();

	return err;
}

uint64_t get_total_size(uint8_t cunit)
{
	return pnm_alloc_total_sz_pool(&alloc, cunit);
}

uint64_t get_free_size(uint8_t cunit)
{
	return pnm_alloc_free_sz_pool(&alloc, cunit);
}

int mem_process_ioctl(struct file *filp, unsigned int cmd,
		      unsigned long __user arg)
{
	int err = 0;
	struct pnm_allocation kreq;
	struct pnm_allocation __user *ureq =
		(struct pnm_allocation __user *)arg;

	SLS_COPY_FROM_USER(err, &kreq, ureq, sizeof(struct pnm_allocation));
	if (unlikely(err)) {
		PNM_ERR("Failed to read user request. Ptr = %p.\n", ureq);
		return err;
	}

	switch (cmd) {
	case ALLOCATE_MEMORY:
		err = allocate_memory_ioctl(filp, &kreq);
		break;
	case DEALLOCATE_MEMORY:
		err = deallocate_memory_ioctl(filp, &kreq);
		break;
	case MAKE_SHARED_ALLOC:
		err = pnm_shared_pool_create_entry(&sls_shared_pool, &kreq);
		break;
	case GET_SHARED_ALLOC:
		err = get_shared_alloc_ioctl(filp, &kreq);
		break;
	default:
		err = -EINVAL;
		PNM_ERR("Unknown memory operation [%u], with argument [%lu]\n",
			cmd, arg);
		return err;
	}

	if (unlikely(err))
		return err;

	SLS_COPY_TO_USER(err, ureq, &kreq, sizeof(struct pnm_allocation));
	if (unlikely(err))
		PNM_ERR("Failed to write user request. Ptr = %p.\n", ureq);

	return err;
}
