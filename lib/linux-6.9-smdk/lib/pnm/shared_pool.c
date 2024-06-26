// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include <linux/pnm/log.h>
#include <linux/pnm/shared_pool.h>
#include <linux/uaccess.h>

static inline bool alloc_less(const struct pnm_allocation *a,
			      const struct pnm_allocation *b)
{
	return a->memory_pool < b->memory_pool ||
	       (a->memory_pool == b->memory_pool && a->addr < b->addr);
}

static struct pnm_shared_pool_entry *
lookup_for_shared_entry(struct rb_root *root,
			const struct pnm_allocation *alloc)
{
	struct rb_node *current_node = root->rb_node;

	while (current_node) {
		struct pnm_shared_pool_entry *entry = container_of(
			current_node, struct pnm_shared_pool_entry, node);

		if (alloc_less(alloc, &entry->alloc))
			current_node = current_node->rb_left;
		else if (alloc_less(&entry->alloc, alloc))
			current_node = current_node->rb_right;
		else
			return entry;
	}
	return NULL;
}

static bool insert_shared_entry(struct rb_root *root,
				struct pnm_shared_pool_entry *new_entry)
{
	struct rb_node **new_node = &(root->rb_node);
	struct rb_node *parent = NULL;

	while (*new_node) {
		struct pnm_shared_pool_entry *current_entry = container_of(
			*new_node, struct pnm_shared_pool_entry, node);

		parent = *new_node;
		if (alloc_less(&new_entry->alloc, &current_entry->alloc))
			new_node = &((*new_node)->rb_left);
		else if (alloc_less(&current_entry->alloc, &new_entry->alloc))
			new_node = &((*new_node)->rb_right);
		else
			return false;
	}

	rb_link_node(&new_entry->node, parent, new_node);
	rb_insert_color(&new_entry->node, root);

	return true;
}

static bool delete_shared_entry(struct rb_root *root,
				struct pnm_shared_pool_entry *entry)
{
	if (entry == NULL)
		return false;

	PNM_DBG("Deleting entry (%p) from shared pool", entry);
	rb_erase(&entry->node, root);
	kfree(entry);
	return true;
}

static int get_alloc_from_shared_entry(struct pnm_shared_pool *pool,
				       struct pnm_allocation *alloc)
{
	struct pnm_shared_pool_entry *entry = NULL;

	mutex_lock(&pool->lock);
	{
		entry = lookup_for_shared_entry(&pool->entry_tree, alloc);
	}
	mutex_unlock(&pool->lock);

	if (entry == NULL)
		return -ENOENT;

	alloc->memory_pool = entry->alloc.memory_pool;
	alloc->addr = entry->alloc.addr;
	alloc->size = entry->alloc.size;
	alloc->is_global = entry->alloc.is_global;
	atomic64_inc(&entry->ref_cnt);

	PNM_DBG("Incrementing counter of entry %p (ref_cnt: %lld)", entry,
		atomic64_read(&entry->ref_cnt));

	return 0;
}

// This function returns:
// - Error, if it was something wrong during execution
// - Value of ref_cnt, if everything was OK. This value serves as
//   a marker, which tells us should we deallocate allocation(ref_cnt == 0)
//   or not (ref_cnt != 0)
static int remove_alloc_from_shared_entry(struct pnm_shared_pool *pool,
					  const struct pnm_allocation *alloc)
{
	struct pnm_shared_pool_entry *entry = NULL;

	mutex_lock(&pool->lock);
	{
		entry = lookup_for_shared_entry(&pool->entry_tree, alloc);
	}
	mutex_unlock(&pool->lock);

	if (entry == NULL)
		return -ENOENT;

	if (atomic64_dec_and_test(&entry->ref_cnt)) {
		mutex_lock(&pool->lock);
		{
			if (!delete_shared_entry(&pool->entry_tree, entry)) {
				mutex_unlock(&pool->lock);
				return -ENOENT;
			}
		}
		mutex_unlock(&pool->lock);
	}

	return atomic64_read(&entry->ref_cnt);
}

static int pnm_shared_pool_create_entry_impl(struct pnm_shared_pool *pool,
					     struct pnm_allocation *alloc)
{
	struct pnm_shared_pool_entry *entry = NULL;

	entry = kzalloc(sizeof(struct pnm_shared_pool_entry), GFP_KERNEL);

	if (entry == NULL)
		return -ENOMEM;

	alloc->is_global = 1;
	entry->alloc = *alloc;
	atomic64_set(&entry->ref_cnt, 1);

	mutex_lock(&pool->lock);
	{
		if (!insert_shared_entry(&pool->entry_tree, entry)) {
			kfree(entry);
			mutex_unlock(&pool->lock);
			return -EINVAL;
		}
	}
	mutex_unlock(&pool->lock);

	PNM_DBG("Create shared entry: %p, ref_cnt: %lld, alloc: %p, addr: %llu, sz: %llu",
		entry, atomic64_read(&entry->ref_cnt), alloc, alloc->addr,
		alloc->size);
	return 0;
}

int pnm_shared_pool_init(struct pnm_shared_pool *pool)
{
	if (pool == NULL)
		return -EINVAL;

	pool->entry_tree = RB_ROOT;
	mutex_init(&pool->lock);
	return 0;
}
EXPORT_SYMBOL(pnm_shared_pool_init);

int pnm_shared_pool_create_entry(struct pnm_shared_pool *pool,
				 struct pnm_allocation *alloc)
{
	if (pool == NULL || alloc == NULL)
		return -EINVAL;

	return pnm_shared_pool_create_entry_impl(pool, alloc);
}
EXPORT_SYMBOL(pnm_shared_pool_create_entry);

int pnm_shared_pool_get_alloc(struct pnm_shared_pool *pool,
			      struct pnm_allocation *alloc)
{
	if (pool == NULL || alloc == NULL)
		return -EINVAL;

	return get_alloc_from_shared_entry(pool, alloc);
}
EXPORT_SYMBOL(pnm_shared_pool_get_alloc);

// This function returns:
// - Error, if it was something wrong during execution
// - Value of ref_cnt, if everything was OK. This value serves as
//   a marker, which tells us should we deallocate allocation(ref_cnt == 0)
//   or not (ref_cnt != 0)
int pnm_shared_pool_remove_alloc(struct pnm_shared_pool *pool,
				 const struct pnm_allocation *alloc)
{
	if (pool == NULL || alloc == NULL)
		return -EINVAL;

	return remove_alloc_from_shared_entry(pool, alloc);
}
EXPORT_SYMBOL(pnm_shared_pool_remove_alloc);

void pnm_shared_pool_cleanup(struct pnm_shared_pool *pool)
{
	struct rb_node *next_node;

	if (pool == NULL) {
		PNM_DBG("Variable 'pool' is not valid");
		return;
	}

	PNM_DBG("Cleanup shared pool");
	mutex_lock(&pool->lock);
	{
		while ((next_node = rb_first(&pool->entry_tree))) {
			struct pnm_shared_pool_entry *entry = rb_entry_safe(
				next_node, struct pnm_shared_pool_entry, node);
			delete_shared_entry(&pool->entry_tree, entry);
		}
	}
	mutex_unlock(&pool->lock);
	mutex_destroy(&pool->lock);
}
EXPORT_SYMBOL(pnm_shared_pool_cleanup);

int pnm_shared_pool_reset(struct pnm_shared_pool *pool)
{
	if (pool == NULL) {
		PNM_DBG("Variable 'pool' is not valid");
		return -EINVAL;
	}

	PNM_DBG("Reset shared pool");
	pnm_shared_pool_cleanup(pool);
	pnm_shared_pool_init(pool);
	return 0;
}
EXPORT_SYMBOL(pnm_shared_pool_reset);
