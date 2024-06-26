/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __PNM_SHARED_POOL_H__
#define __PNM_SHARED_POOL_H__

#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/pnm_resources.h>
#include <linux/rbtree.h>

struct pnm_shared_pool_entry {
	struct rb_node node;
	struct pnm_allocation alloc;
	atomic64_t ref_cnt;
};

struct pnm_shared_pool {
	struct rb_root entry_tree;
	struct mutex lock;
};

int pnm_shared_pool_init(struct pnm_shared_pool *pool);
void pnm_shared_pool_cleanup(struct pnm_shared_pool *pool);
int pnm_shared_pool_reset(struct pnm_shared_pool *pool);

int pnm_shared_pool_create_entry(struct pnm_shared_pool *pool,
				 struct pnm_allocation *alloc);
int pnm_shared_pool_get_alloc(struct pnm_shared_pool *pool,
			      struct pnm_allocation *alloc);
int pnm_shared_pool_remove_alloc(struct pnm_shared_pool *pool,
				 const struct pnm_allocation *alloc);

#endif /* __PNM_SHARED_POOL_H__ */
