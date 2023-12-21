// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2023 Samsung LTD. All rights reserved.

#include "proc_mgr.h"

#include "allocator.h"
#include "thread_sched.h"
#include "topo/params.h"

#include "linux/imdb_resources.h"
#include "linux/ioport.h"
#include "linux/stddef.h"
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pnm/log.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>

/* list of descriptors held by process */
struct imdb_alloc_res {
	struct pnm_allocation alloc;
	struct rb_node node;
};

/* resources allocated for particular process */
struct imdb_proc_res {
	struct rb_root alloc_res_tree;
	/* Must be protected by imdb_proc_lock */
	uint8_t threads_mask;
	/* This counter needed because each open syscall create new
	 * file descriptors and call register process.
	 * Sometimes userspace process can just open/release device
	 * for reset ioctl purpose. take in mind this counter should be accessed
	 * only under lock in order to prevent race conditions
	 */
	struct list_head list;
	struct mutex imdb_proc_lock;
};

/*
 * data structure for tracking device resources, allocated to user space
 * processes
 */
struct proc_mgr {
	struct list_head leaked_process_list;
	atomic64_t leaked;
	atomic64_t enable_cleanup;
	/* mutex for accessing process_list concurrently */
	struct mutex lock;
};

static struct proc_mgr proc_mgr = {
	.leaked_process_list = LIST_HEAD_INIT(proc_mgr.leaked_process_list),
	.leaked = ATOMIC64_INIT(0),
	.enable_cleanup = ATOMIC64_INIT(IMDB_DISABLE_CLEANUP),
	.lock = __MUTEX_INITIALIZER(proc_mgr.lock),
};

static inline bool imdb_alloc_less(struct pnm_allocation a,
				   struct pnm_allocation b)
{
	return a.memory_pool < b.memory_pool ||
	       (a.memory_pool == b.memory_pool && a.addr < b.addr);
}

static bool desc_delete(struct rb_root *root,
			const struct pnm_allocation *alloc)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct imdb_alloc_res *desc_node =
			container_of(node, struct imdb_alloc_res, node);

		if (imdb_alloc_less(*alloc, desc_node->alloc)) {
			node = node->rb_left;
		} else if (imdb_alloc_less(desc_node->alloc, *alloc)) {
			node = node->rb_right;
		} else {
			rb_erase(&desc_node->node, root);
			kfree(desc_node);
			return true;
		}
	}
	return false;
}

static bool desc_insert(struct rb_root *root,
			const struct pnm_allocation *alloc)
{
	struct rb_node **new = &(root->rb_node);
	struct rb_node *parent = NULL;
	struct imdb_alloc_res *imdb_alloc = NULL;

	imdb_alloc = kzalloc(sizeof(*imdb_alloc), GFP_KERNEL);

	if (imdb_alloc)
		imdb_alloc->alloc = *alloc;
	else
		return false;

	while (*new) {
		struct imdb_alloc_res *this =
			container_of(*new, struct imdb_alloc_res, node);

		parent = *new;
		if (imdb_alloc_less(*alloc, this->alloc))
			new = &((*new)->rb_left);
		else if (imdb_alloc_less(this->alloc, *alloc))
			new = &((*new)->rb_right);
		else
			return false;
	}

	rb_link_node(&imdb_alloc->node, parent, new);
	rb_insert_color(&imdb_alloc->node, root);

	return true;
}

static int clear_allocations(struct imdb_proc_res *proc_res)
{
	struct pnm_allocation *alloc = NULL;
	struct rb_node *next;
	uint64_t begin = 0;
	uint64_t end = 0;
	int rc = 0;

	while ((next = rb_first(&proc_res->alloc_res_tree))) {
		struct imdb_alloc_res *alloc_res_node =
			rb_entry_safe(next, struct imdb_alloc_res, node);

		alloc = &alloc_res_node->alloc;
		begin = alloc->addr;
		end = begin + alloc->size;
		PNM_DBG("Process manager release allocation [%llx, %llx]\n",
			begin, end);

		/*
		 * Same as for sls_resource. We should work with allocator
		 * and rb_tree in process manager under one mutex for now.
		 */
		imdb_alloc_lock();
		{
			rc |= allocator_clear_res(alloc);
			rb_erase(&alloc_res_node->node,
				 &proc_res->alloc_res_tree);
			kfree(alloc_res_node);
		}
		imdb_alloc_unlock();
	}

	if (rc) {
		PNM_ERR("Can't clear allocations\n");
		rc = -EINVAL;
	}

	return rc;
}

static int clear_threads(struct imdb_proc_res *proc_res)
{
	int rc = 0;
	uint8_t it = 0;

	for (it = 0; it < imdb_topo()->nr_cunits; ++it) {
		if (proc_res->threads_mask & (1 << it)) {
			rc |= thread_sched_clear_res(it);
			PNM_DBG("Process manager release thread[%d]\n", it);
		}
	}

	if (rc) {
		PNM_ERR("Can't clear threads\n");
		return -EINVAL;
	}

	return rc;
}

static int clear_process_resource(struct imdb_proc_res *proc_res)
{
	int failed = 0;

	failed |= clear_threads(proc_res);
	failed |= clear_allocations(proc_res);
	mutex_destroy(&proc_res->imdb_proc_lock);
	kfree(proc_res);

	return failed ? -1 : 0;
}

static inline bool is_resource_empty(struct imdb_proc_res *proc_res)
{
	return RB_EMPTY_ROOT(&proc_res->alloc_res_tree) &&
	       proc_res->threads_mask == 0;
}

int imdb_register_allocation(struct file *filp,
			     const struct pnm_allocation *alloc)
{
	struct imdb_proc_res *proc_res = NULL;
	int rc = 0;

	if (!filp)
		return -EINVAL;

	proc_res = (struct imdb_proc_res *)filp->private_data;

	if (likely(proc_res)) {
		if (!desc_insert(&proc_res->alloc_res_tree, alloc)) {
			PNM_ERR("Can't register allocation\n");
			rc = -ENOMEM;
		}
	} else {
		PNM_ERR("Can't find resources\n");
		rc = -ESRCH;
	}

	return rc;
}

int imdb_unregister_allocation(struct file *filp,
			       const struct pnm_allocation *alloc)
{
	struct imdb_proc_res *proc_res = NULL;
	int rc = 0;

	if (!filp)
		return -EINVAL;

	proc_res = (struct imdb_proc_res *)filp->private_data;

	if (likely(proc_res)) {
		if (!desc_delete(&proc_res->alloc_res_tree, alloc)) {
			rc = -EINVAL;
			PNM_ERR("Allocation not found\n");
		}
	} else {
		PNM_ERR("Can't find resources\n");
		rc = -ESRCH;
	}

	return rc;
}

int imdb_register_thread(struct file *filp, uint8_t thread)
{
	struct imdb_proc_res *proc_res = NULL;
	int rc = 0;
	const uint8_t thread_mask = 1 << thread;

	if (!filp)
		return -EINVAL;

	proc_res = (struct imdb_proc_res *)filp->private_data;

	if (likely(proc_res)) {
		mutex_lock(&proc_res->imdb_proc_lock);
		proc_res->threads_mask |= thread_mask;
		mutex_unlock(&proc_res->imdb_proc_lock);
	} else {
		PNM_ERR("Can't find resources\n");
		rc = -ESRCH;
	};

	return rc;
}

int imdb_unregister_thread(struct file *filp, uint8_t thread)
{
	struct imdb_proc_res *proc_res = NULL;
	const uint8_t thread_mask = ~(1 << thread);
	int rc = 0;

	if (!filp)
		return -EINVAL;

	proc_res = (struct imdb_proc_res *)filp->private_data;

	if (likely(proc_res)) {
		mutex_lock(&proc_res->imdb_proc_lock);
		proc_res->threads_mask &= thread_mask;
		mutex_unlock(&proc_res->imdb_proc_lock);
	} else {
		PNM_ERR("Can't find resources\n");
		rc = -ESRCH;
	}

	return rc;
}

int imdb_register_process(struct file *filp)
{
	struct imdb_proc_res *proc_res = NULL;
	int rc = 0;

	if (!filp)
		return -EINVAL;

	proc_res = (struct imdb_proc_res *)filp->private_data;

	if (!proc_res) {
		proc_res = kzalloc(sizeof(*proc_res), GFP_KERNEL);

		if (unlikely(!proc_res)) {
			rc = -ENOMEM;
			PNM_ERR("Can't allocate memory\n");
		} else {
			proc_res->alloc_res_tree = RB_ROOT;
			mutex_init(&proc_res->imdb_proc_lock);
			filp->private_data = proc_res;
			PNM_DBG("Registered process\n");
		}
	}

	return rc;
}

int imdb_release_process(struct file *filp)
{
	struct imdb_proc_res *proc_res = NULL;
	int rc = 0;

	if (!filp)
		return -EINVAL;

	proc_res = (struct imdb_proc_res *)filp->private_data;

	if (unlikely(!proc_res)) {
		PNM_ERR("Can't find resources\n");
		return -ESRCH;
	}

	if (is_resource_empty(proc_res)) {
		kfree(proc_res);
		return rc;
	}

	if (atomic64_read(&proc_mgr.enable_cleanup)) {
		rc = clear_process_resource(proc_res);
		if (rc)
			PNM_ERR("Can't clear process resources\n");
		return rc;
	}

	mutex_lock(&proc_mgr.lock);
	{
		atomic64_inc(&proc_mgr.leaked);
		list_add(&proc_res->list, &proc_mgr.leaked_process_list);
		PNM_DBG("Tracking leakage\n");
	}
	mutex_unlock(&proc_mgr.lock);

	return rc;
}

uint64_t imdb_get_leaked(void)
{
	return atomic64_read(&proc_mgr.leaked);
}

void imdb_disable_cleanup(void)
{
	atomic64_set(&proc_mgr.enable_cleanup, IMDB_DISABLE_CLEANUP);
}

int imdb_enable_cleanup(void)
{
	LIST_HEAD(list_tmp);
	struct imdb_proc_res *proc_res = NULL, *proc_res_tmp = NULL;
	int rc = 0;

	mutex_lock(&proc_mgr.lock);
	{
		atomic64_set(&proc_mgr.enable_cleanup, IMDB_ENABLE_CLEANUP);

		atomic64_set(&proc_mgr.leaked, 0);
		// splice list for critical section minimization
		list_splice_init(&proc_mgr.leaked_process_list, &list_tmp);
	}
	mutex_unlock(&proc_mgr.lock);

	list_for_each_entry_safe(proc_res, proc_res_tmp, &list_tmp, list) {
		list_del(&proc_res->list);
		rc |= clear_process_resource(proc_res);
	}

	return rc ? -1 : 0;
}

bool imdb_get_proc_manager(void)
{
	return atomic64_read(&proc_mgr.enable_cleanup) == IMDB_ENABLE_CLEANUP;
}

int imdb_reset_proc_manager(void)
{
	LIST_HEAD(list_leaked_tmp);

	struct imdb_proc_res *proc_res = NULL, *proc_res_tmp = NULL;

	bool have_leaks = false;

	mutex_lock(&proc_mgr.lock);
	{
		// splice list for critical section minimization
		list_splice_init(&proc_mgr.leaked_process_list,
				 &list_leaked_tmp);

		have_leaks = atomic64_read(&proc_mgr.leaked) != 0;
	}
	mutex_unlock(&proc_mgr.lock);

	list_for_each_entry_safe(proc_res, proc_res_tmp, &list_leaked_tmp,
				 list) {
		kfree(proc_res);
	}

	return have_leaks ? -1 : 0;
}

void imdb_destroy_proc_manager(void)
{
	imdb_reset_proc_manager();
	mutex_destroy(&proc_mgr.lock);
}
