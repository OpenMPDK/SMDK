// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include "process_manager.h"
#include "allocator.h"
#include "cunit_sched.h"

#include <linux/pnm/log.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>

struct sls_proc_desc {
	struct pnm_allocation alloc;
	struct rb_node node;
};

/* resources allocated for particular process */
struct sls_proc_resources {
	struct rb_root alloc_desc_tree;
	unsigned long cunit_mask;
	/* this counter needed because release f_op is called on each close
	 * syscall, sometimes userspace process can just open/close device
	 * for reset ioctl purpose. take in mind this counter should be accessed
	 * only under proc_list_lock in order to prevent race conditions
	 */
	int ref_cnt;
	struct list_head list;
	/* allows to work with fields in multithreaded environment */
	struct mutex sls_proc_lock;
};

/*
 * data structure for tracking device resources, allocated to user space
 * processes
 */
struct process_manager {
	/* [TODO:] need to checkout performance impact of using just list,
	 * maybe some more advanced data structure is required (rb_tree)
	 */
	atomic64_t enable_cleanup;
	struct list_head leaked_process_list;
	atomic64_t leaked;
	/* mutex for accessing process_list concurrently */
	struct mutex proc_list_lock;
};

static struct process_manager proc_mgr = {
	.enable_cleanup = ATOMIC64_INIT(0),
	.leaked_process_list = LIST_HEAD_INIT(proc_mgr.leaked_process_list),
	.leaked = ATOMIC64_INIT(0),
	.proc_list_lock = __MUTEX_INITIALIZER(proc_mgr.proc_list_lock)
};

static inline bool sls_alloc_less(struct pnm_allocation a,
				  struct pnm_allocation b)
{
	return a.memory_pool < b.memory_pool ||
	       (a.memory_pool == b.memory_pool && a.addr < b.addr);
}

static bool desc_delete(struct rb_root *root, struct pnm_allocation alloc)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct sls_proc_desc *desc_node =
			container_of(node, struct sls_proc_desc, node);

		if (sls_alloc_less(alloc, desc_node->alloc)) {
			node = node->rb_left;
		} else if (sls_alloc_less(desc_node->alloc, alloc)) {
			node = node->rb_right;
		} else {
			rb_erase(&desc_node->node, root);
			kfree(desc_node);
			return true;
		}
	}
	return false;
}

static bool desc_insert(struct rb_root *root, struct sls_proc_desc *proc_desc)
{
	struct rb_node **new = &(root->rb_node);
	struct rb_node *parent = NULL;

	while (*new) {
		struct sls_proc_desc *this =
			container_of(*new, struct sls_proc_desc, node);

		parent = *new;
		if (sls_alloc_less(proc_desc->alloc, this->alloc))
			new = &((*new)->rb_left);
		else if (sls_alloc_less(this->alloc, proc_desc->alloc))
			new = &((*new)->rb_right);
		else
			return false;
	}

	rb_link_node(&proc_desc->node, parent, new);
	rb_insert_color(&proc_desc->node, root);

	return true;
}

static pid_t get_current_process_id(void)
{
	/*
	 * group_leader is a pointer to the task_struct of the main thread of
	 * userspace process.
	 */
	return current->group_leader->pid;
}

static bool has_resources_leaked(struct sls_proc_resources *proc_res)
{
	return proc_res->cunit_mask ||
	       !RB_EMPTY_ROOT(&proc_res->alloc_desc_tree);
}

static void track_leaked_resources(struct process_manager *mgr,
				   struct sls_proc_resources *proc_res)
{
	struct rb_node *node;
	struct sls_proc_desc *desc;

	atomic64_inc(&mgr->leaked);
	list_add(&proc_res->list, &mgr->leaked_process_list);

	PNM_DBG("Tracked leakage by pid: %d, tid: %d; cunit_mask: %lu\n",
		get_current_process_id(), current->pid, proc_res->cunit_mask);

	for (node = rb_first(&proc_res->alloc_desc_tree); node;
	     node = rb_next(node)) {
		desc = rb_entry(node, struct sls_proc_desc, node);
		PNM_DBG("Leaked memory under desc[cunit = %hhu, addr = %llu]\n",
			desc->alloc.memory_pool, desc->alloc.addr);
	}
}

static bool release_cunit_mask(unsigned long *cunit_mask)
{
	bool failed = false;
	unsigned long bit_index;

	while (*cunit_mask) {
		bit_index = ffs(*cunit_mask) - 1;
		failed |= (release_cunit(bit_index) != bit_index);
		clear_bit(bit_index, cunit_mask);
		PNM_INF("Abnormal release cunit[%lu], pid: %d, tid: %d\n",
			bit_index, get_current_process_id(), current->pid);
	}

	return failed;
}

static int release_process_resources(struct sls_proc_resources *proc_res)
{
	/* in base case scenario the resources above should be already
	 * released, but we need to check in case we are on
	 * 'killed process' path
	 */
	struct rb_node *next;
	bool failed = false;

	/* handle unreleased cunits */
	failed |= release_cunit_mask(&proc_res->cunit_mask);

	/* handle unreleased allocation descriptors */
	while ((next = rb_first(&proc_res->alloc_desc_tree))) {
		struct sls_proc_desc *desc_node =
			rb_entry_safe(next, struct sls_proc_desc, node);

		/*
		 * [TODO: @e-kutovoi] Genalloc allocations and the RB-tree
		 * in process manager must be updated atomically. For now just
		 * put both of these behind one mutex lock. But this is very
		 * annoying to use. So we should instead refactor the code, and
		 * perhaps move the RB-tree from process manager into the allocator.
		 */
		lock_sls_allocator();
		{
			//[TODO:s-motov] kcsan check

			failed |= deallocate_memory_unsafe(desc_node->alloc) !=
				  0;
			PNM_INF("Abnormal release desc[cunit=%hhu, offset=%llu],pid:%d,tid:%d\n",
				desc_node->alloc.memory_pool,
				desc_node->alloc.addr, get_current_process_id(),
				current->pid);
			next = rb_first(&proc_res->alloc_desc_tree);
			rb_erase(&desc_node->node, &proc_res->alloc_desc_tree);
			kfree(desc_node);
		}
		unlock_sls_allocator();
	}
	mutex_destroy(&proc_res->sls_proc_lock);

	return failed ? -1 : 0;
}

int release_sls_process(struct file *filp)
{
	int err_code = 0;
	struct sls_proc_resources *proc_res = NULL;
	bool res_leaked;
	bool free_res = false;

	if (!filp)
		return -EINVAL;

	proc_res = (struct sls_proc_resources *)filp->private_data;

	if (!proc_res) {
		PNM_ERR("Tried to release already released process by pid: %d, tid: %d\n",
			get_current_process_id(), current->pid);
		return -EINVAL;
	}

	mutex_lock(&proc_res->sls_proc_lock);
	proc_res->ref_cnt--;
	if (proc_res->ref_cnt == 0)
		free_res = true;
	mutex_unlock(&proc_res->sls_proc_lock);

	/* release resources if:
	 *   - we are in last close syscall from userspace
	 *   - process got finished abnormally
	 */
	if (free_res) {
		PNM_DBG("Releasing process pid: %d, tid: %d\n",
			get_current_process_id(), current->pid);

		res_leaked = has_resources_leaked(proc_res);

		if (!res_leaked)
			goto free_proc_res;

		if (atomic64_read(&proc_mgr.enable_cleanup)) {
			err_code = release_process_resources(proc_res);
		} else {
			mutex_lock(&proc_mgr.proc_list_lock);
			{
				ASSERT_EXCLUSIVE_ACCESS_SCOPED(
					proc_mgr.leaked_process_list);
				track_leaked_resources(&proc_mgr, proc_res);
			}
			mutex_unlock(&proc_mgr.proc_list_lock);
			goto out;
		}
free_proc_res:
		kfree(proc_res);
	}
out:
	return err_code;
}

static int set_cunit_status(struct file *filp, uint8_t cunit, bool set)
{
	struct sls_proc_resources *proc_res = NULL;

	if (!filp)
		return -EINVAL;

	proc_res = (struct sls_proc_resources *)filp->private_data;

	if (proc_res == NULL)
		return -EINVAL;

	/* set or unset cunit's bit according to request */
	assign_bit(cunit, &proc_res->cunit_mask, set);
	return 0;
}

int sls_proc_register_cunit(struct file *filp, uint8_t cunit)
{
	PNM_DBG("Registering cunit[%hhu], pid: %d, tid: %d\n", cunit,
		get_current_process_id(), current->pid);
	if (set_cunit_status(filp, cunit, true)) {
		PNM_ERR("Fail to register cunit[%hhu], pid: %d, tid: %d\n",
			cunit, get_current_process_id(), current->pid);
		return -1;
	}
	return 0;
}

int sls_proc_remove_cunit(struct file *filp, uint8_t cunit)
{
	PNM_DBG("Removing cunit[%hhu], pid: %d, tid: %d\n", cunit,
		get_current_process_id(), current->pid);
	if (set_cunit_status(filp, cunit, false)) {
		PNM_ERR("Fail to remove cunit[%hhu], pid: %d, tid: %d\n", cunit,
			get_current_process_id(), current->pid);
		return -1;
	}
	return 0;
}

static int update_alloc_tree(struct file *filp, struct pnm_allocation alloc,
			     bool is_registration)
{
	int err_code = 0;
	struct sls_proc_resources *proc_res = NULL;
	struct sls_proc_desc *proc_desc = NULL;

	if (!filp)
		return -EINVAL;

	proc_res = (struct sls_proc_resources *)filp->private_data;

	if (proc_res == NULL)
		return -EINVAL;

	mutex_lock(&proc_res->sls_proc_lock);
	if (is_registration) { /* allocate and add descritor to descriptors list */
		proc_desc = kzalloc(sizeof(struct sls_proc_desc), GFP_KERNEL);

		if (proc_desc == NULL) {
			err_code = -1;
			goto unlock_mutex_out;
		}

		proc_desc->alloc = alloc;
		if (!desc_insert(&proc_res->alloc_desc_tree, proc_desc))
			err_code = -1;
	} else { /* remove requested descriptor from descriptors list */
		if (!desc_delete(&proc_res->alloc_desc_tree, alloc))
			err_code = -1;
	}

unlock_mutex_out:
	mutex_unlock(&proc_res->sls_proc_lock);
	return err_code;
}

static int register_allocation(struct file *filp, struct pnm_allocation alloc)
{
	return update_alloc_tree(filp, alloc, true);
}

static int unregister_allocation(struct file *filp, struct pnm_allocation alloc)
{
	return update_alloc_tree(filp, alloc, false);
}

int sls_proc_register_alloc(struct file *filp, struct pnm_allocation alloc)
{
	PNM_DBG("Registering allocation, desc[cunit = %hhu, addr = %llu], pid: %d, tid: %d\n",
		alloc.memory_pool, alloc.addr, get_current_process_id(),
		current->pid);
	if (register_allocation(filp, alloc)) {
		PNM_ERR("Fail to register cunit: %hhu, addr: %llu, pid: %d, tid: %d\n",
			alloc.memory_pool, alloc.addr, get_current_process_id(),
			current->pid);
		return -1;
	}
	return 0;
}

int sls_proc_remove_alloc(struct file *filp, struct pnm_allocation alloc)
{
	PNM_DBG("Removing allocation, desc[cunit = %hhu, addr = %llu], pid: %d, tid: %d\n",
		alloc.memory_pool, alloc.addr, get_current_process_id(),
		current->pid);
	if (unregister_allocation(filp, alloc)) {
		PNM_ERR("Fail to remove cunit: %hhu, addr: %llu, pid: %d, tid: %d\n",
			alloc.memory_pool, alloc.addr, get_current_process_id(),
			current->pid);
		return -1;
	}
	return 0;
}

int register_sls_process(struct file *filp)
{
	struct sls_proc_resources *proc_res = NULL;
	pid_t pid = get_current_process_id();

	if (!filp)
		return -EINVAL;

	proc_res = (struct sls_proc_resources *)filp->private_data;

	if (proc_res)
		goto inc_ref_count;

	proc_res = kzalloc(sizeof(struct sls_proc_resources), GFP_KERNEL);

	if (!proc_res) {
		PNM_ERR("Failed to register process, pid: %d, tid: %d\n", pid,
			current->pid);
		return -ENOMEM;
	}

	proc_res->alloc_desc_tree = RB_ROOT;
	mutex_init(&proc_res->sls_proc_lock);
	filp->private_data = proc_res;

	PNM_DBG("Registered process, pid: %d, tid: %d\n", pid, current->pid);
inc_ref_count:
	mutex_lock(&proc_res->sls_proc_lock);
	proc_res->ref_cnt++;
	mutex_unlock(&proc_res->sls_proc_lock);

	return 0;
}

int sls_proc_manager_cleanup_on(void)
{
	struct sls_proc_resources *proc_res_tmp;
	struct sls_proc_resources *proc_res;
	int err_code = 0;

	PNM_DBG("Enabling cleanup\n");

	atomic64_set(&proc_mgr.enable_cleanup, 1);

	/* cleanup leaked_process_list */
	mutex_lock(&proc_mgr.proc_list_lock);
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(proc_mgr.leaked_process_list);

		list_for_each_entry_safe(proc_res, proc_res_tmp,
					 &proc_mgr.leaked_process_list, list) {
			err_code |= release_process_resources(proc_res);
			list_del(&proc_res->list);
			kfree(proc_res);
		}
		atomic64_set(&proc_mgr.leaked, 0);
	}
	mutex_unlock(&proc_mgr.proc_list_lock);

	return err_code ? -1 : 0;
}

void sls_proc_manager_cleanup_off(void)
{
	PNM_DBG("Disabling cleanup\n");
	atomic64_set(&proc_mgr.enable_cleanup, 0);
}

void cleanup_process_list(struct list_head *process_list)
{
	struct sls_proc_resources *proc_res_tmp;
	struct sls_proc_resources *proc_res;

	list_for_each_entry_safe(proc_res, proc_res_tmp, process_list, list) {
		list_del(&proc_res->list);
		kfree(proc_res);
	}
}

void reset_process_manager(void)
{
	cleanup_process_list(&proc_mgr.leaked_process_list);
}

void cleanup_process_manager(void)
{
	struct sls_proc_resources *proc_res_tmp;
	struct sls_proc_resources *proc_res;

	mutex_lock(&proc_mgr.proc_list_lock);
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(proc_mgr.leaked_process_list);

		list_for_each_entry_safe(proc_res, proc_res_tmp,
					 &proc_mgr.leaked_process_list, list) {
			list_del(&proc_res->list);
			kfree(proc_res);
		}
	}
	mutex_unlock(&proc_mgr.proc_list_lock);
}

uint64_t sls_proc_mgr_leaked(void)
{
	return atomic64_read(&proc_mgr.leaked);
}

bool sls_proc_mgr_cleanup(void)
{
	return atomic64_read(&proc_mgr.enable_cleanup) == 1;
}
