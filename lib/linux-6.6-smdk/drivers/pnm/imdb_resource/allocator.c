// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#include "allocator.h"
#include "device_resource.h"
#include "private.h"
#include "proc_mgr.h"
#include "topo/params.h"

#include <linux/imdb_resources.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/numa.h>
#include <linux/pnm/alloc.h>
#include <linux/pnm/log.h>
#include <linux/pnm/shared_pool.h>
#include <linux/uaccess.h>

static struct pnm_alloc alloc;
static struct pnm_shared_pool imdb_shared_pool;

void imdb_alloc_lock(void)
{
	pnm_alloc_lock(&alloc);
}

void imdb_alloc_unlock(void)
{
	pnm_alloc_unlock(&alloc);
}

/* Use this function under allocator_lock mutex */
static int allocate_memory(struct pnm_allocation *req)
{
	if (req->memory_pool == imdb_topo()->nr_pools)
		req->memory_pool = pnm_alloc_most_free_pool(&alloc);

	return pnm_alloc_pool_malloc(&alloc, req->memory_pool, req->size,
				     &req->addr);
}

/* Use this function under allocator_lock mutex */
static int deallocate_memory(const struct pnm_allocation *req)
{
	uint64_t addr;

	if (req->is_global == 1) {
		int rc = 0;

		rc = pnm_shared_pool_remove_alloc(&imdb_shared_pool, req);
		if (rc)
			return rc;
	}

	addr = req->addr;

	return pnm_alloc_pool_free(&alloc, req->memory_pool, req->size, &addr);
}

/* Use this function under allocator_lock mutex */
int allocator_clear_res(const struct pnm_allocation *req)
{
	deallocate_memory(req);
	return 0;
}

int allocator_ioctl(struct file *filp, unsigned int cmd,
		    unsigned long __user arg)
{
	struct pnm_allocation req;

	int rc = !!copy_from_user(&req, (void __user *)arg, sizeof(req));

	if (rc)
		return -EFAULT;

	if (req.memory_pool > imdb_topo()->nr_pools)
		return -EINVAL;

	/*
	 * When we allocate or deallocate memory and register or unregister
	 * this resource in process manager(rb_tree), we should do it
	 * using mutex here. Otherwise, we will have "Out of memory"
	 * error in some cases, which involve multithreading.
	 */
	switch (cmd) {
	case IMDB_IOCTL_ALLOCATE:
		imdb_alloc_lock();
		{
			rc = allocate_memory(&req);
			if (!rc)
				rc = imdb_register_allocation(filp, &req);
		}
		imdb_alloc_unlock();
		break;
	case IMDB_IOCTL_DEALLOCATE:
		imdb_alloc_lock();
		{
			rc = imdb_unregister_allocation(filp, &req);
			if (!rc)
				rc = deallocate_memory(&req);
		}
		imdb_alloc_unlock();
		break;
	case IMDB_IOCTL_MAKE_SHARED_ALLOC:
		rc = pnm_shared_pool_create_entry(&imdb_shared_pool, &req);
		break;
	case IMDB_IOCTL_GET_SHARED_ALLOC:
		imdb_alloc_lock();
		{
			rc = pnm_shared_pool_get_alloc(&imdb_shared_pool, &req);
			if (!rc)
				rc = imdb_register_allocation(filp, &req);
		}
		imdb_alloc_unlock();
		break;
	}

	if (rc)
		return rc;

	rc = !!copy_to_user((void __user *)arg, &req, sizeof(req));

	if (rc)
		return -EFAULT;

	return rc;
}

uint64_t get_avail_size(void)
{
	return pnm_alloc_free_sz(&alloc);
}

uint64_t mem_size_in_bytes(void)
{
	return ((uint64_t)imdb_topo()->mem_size_gb) << ONE_GB_SHIFT;
}

uint64_t get_granularity(void)
{
	return pnm_alloc_granularity(&alloc);
}

int initialize_memory_allocator(void)
{
	uint64_t start = 0, gran = IMDB_MEMORY_ADDRESS_ALIGN;
	uint8_t idx;
	uint8_t nr_pools = imdb_topo()->nr_pools;
	uint64_t pool_size = mem_size_in_bytes() / nr_pools;
	struct range *ranges =
		kcalloc(nr_pools, sizeof(struct range), GFP_KERNEL);
	int err_code;

	PNM_DBG("Initializing IMDB allocator\n");

	if (!ranges)
		return -ENOMEM;
	for (idx = 0; idx < nr_pools; ++idx, start += pool_size)
		ranges[idx] = (struct range){
			start,
			start + pool_size - 1,
		};

	err_code = pnm_alloc_init(&alloc, gran, nr_pools, ranges);
	if (!err_code) {
		pnm_alloc_set_force_aligned(&alloc, true);
		err_code = pnm_shared_pool_init(&imdb_shared_pool);
		if (err_code)
			pnm_alloc_cleanup(&alloc);
	}

	kfree(ranges);

	return err_code;
}

void destroy_memory_allocator(void)
{
	PNM_DBG("Destroy IMDB allocator\n");
	pnm_shared_pool_cleanup(&imdb_shared_pool);
	pnm_alloc_cleanup(&alloc);
}

int reset_memory_allocator(void)
{
	int rc = 0;
	PNM_INF("Reset allocator");
	rc = pnm_shared_pool_reset(&imdb_shared_pool);
	if (unlikely(rc))
		return rc;
	return pnm_alloc_reset(&alloc);
}
