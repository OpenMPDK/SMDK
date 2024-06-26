#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/frontswap.h>
#include <linux/rbtree.h>
#include <linux/swap.h>
#include <linux/cxlpool.h>

#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/swapops.h>
#include <linux/workqueue.h>
#include <linux/writeback.h>
#include <linux/mempolicy.h>

#include "swap.h"

/*
 * statistics
 */

u64 cxlswap_pool_total_size;

atomic_t cxlswap_stored_pages = ATOMIC_INIT(0);
/* The number of same-value filled pages currently stored in cxlswap */
static atomic_t cxlswap_same_filled_pages = ATOMIC_INIT(0);

static u64 cxlswap_pool_limit_hit;

static u64 cxlswap_written_back_pages;

static u64 cxlswap_reject_reclaim_fail;

static u64 cxlswap_reject_alloc_fail;

static u64 cxlswap_reject_kmemcache_fail;

static struct workqueue_struct *shrink_wq;
static struct workqueue_struct *flush_wq;

static bool cxlswap_pool_reached_full;

/*
 * tunables
 */
static bool cxlswap_enabled = IS_ENABLED(CONFIG_CXLSWAP_DEFAULT_ON);
static int cxlswap_enabled_param_set(const char *,
				const struct kernel_param *);
static const struct kernel_param_ops cxlswap_enabled_param_ops = {
	.set = 		cxlswap_enabled_param_set,
	.get = 		param_get_bool,
};
module_param_cb(enabled, &cxlswap_enabled_param_ops, &cxlswap_enabled, 0644);

static char *cxlswap_cxlpool_type = CONFIG_CXLSWAP_CXLPOOL_DEFAULT;
static int cxlswap_cxlpool_param_set(const char *, const struct kernel_param *);
static const struct kernel_param_ops cxlswap_cxlpool_param_ops = {
	.set =		cxlswap_cxlpool_param_set,
	.get = 		param_get_charp,
	.free =		param_free_charp,
};
module_param_cb(cxlpool, &cxlswap_cxlpool_param_ops,
				&cxlswap_cxlpool_type, 0644);

static unsigned int cxlswap_max_pool_percent = 20;
module_param_named(max_pool_percent, cxlswap_max_pool_percent, uint, 0644);

static unsigned int cxlswap_accept_thr_percent = 90;
module_param_named(accept_threshold_percent, cxlswap_accept_thr_percent,
				uint, 0644);

/*
 * Enable/disable handling same-value filled pages (enabled by default).
 * If disabled every page is considered non-same-value filled.
 */
static bool cxlswap_same_filled_pages_enabled = true;
module_param_named(same_filled_pages_enabled, cxlswap_same_filled_pages_enabled,
				bool, 0644);

/* Enable/disable handling non-same-value filled pages (enabled by default) */
static bool cxlswap_non_same_filled_pages_enabled = true;
module_param_named(non_same_filled_pages_enabled, cxlswap_non_same_filled_pages_enabled,
		   bool, 0644);
/* 
 * [Experimental] 
 * Flush CXL Swap entry to Disk. 
 * It works only when the CXL Swap is disabled.
 *
 * There are two case when flush working on.
 * 1. Entry's refcount is not zero. (already in swap cache or not)
 * 2. Entry's refcount is zero. (lazy freed by swap slot cache context)
 *
 * The first case, we treat them as evict. 
 * If entry already in swap cache, then just free it. 
 * Anyway, CXL Swap is disabled  so it doesn't matter.
 * The second case, we just free it. 
 * Entry refcount is zero to be freed layer anyway by invalidate.
 * Flush just free it aggressively for N-Way composibility.
 *
 */
static bool cxlswap_flush;
static int cxlswap_flush_param_set(const char *, const struct kernel_param *);
static const struct kernel_param_ops cxlswap_flush_param_ops = {
	.set =		cxlswap_flush_param_set,
	.get = 		NULL,
};
module_param_cb(flush, &cxlswap_flush_param_ops, &cxlswap_flush, 0200);

/*
 * data structures
 */

struct cxlswap_pool {
	struct cxlpool *cxlpool;
	struct kref kref;
	struct list_head list;
	struct work_struct release_work;
	struct work_struct shrink_work;
	struct work_struct flush_work;
	struct hlist_node node;
	struct completion done;
};

struct cxlswap_header {
	swp_entry_t swpentry;
};

struct cxlswap_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	int refcount;
	unsigned int length;
	struct cxlswap_pool *pool;
	union {
		unsigned long handle;
		unsigned long value;
	};
};

struct cxlswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};

static struct cxlswap_tree *cxlswap_trees[MAX_SWAPFILES];

static LIST_HEAD(cxlswap_pools);

static DEFINE_SPINLOCK(cxlswap_pools_lock);

static atomic_t cxlswap_pools_count = ATOMIC_INIT(0);

static bool cxlswap_init_started;

static bool cxlswap_init_failed;

static bool cxlswap_has_pool;

/*
 * helpers and fwd declarations
 */

static int cxlswap_writeback_entry(struct cxlpool *pool, unsigned long handle,
				bool under_flush);
static int cxlswap_pool_get(struct cxlswap_pool *pool);
static void cxlswap_pool_put(struct cxlswap_pool *pool);

static const struct cxlpool_ops cxlswap_cxlpool_ops = {
	.evict = cxlswap_writeback_entry
};

/* Get total number of pages on CXL memory */
static inline unsigned long totalexmem_pages(void)
{
	unsigned long _totalexmem_pages = 0;
	int nid;

	for_each_online_node(nid) {
		struct zone *zone = &NODE_DATA(nid)->node_zones[ZONE_MOVABLE];
		if (zone_has_subzone(zone))
			_totalexmem_pages += zone_managed_pages(zone);
	}

	return _totalexmem_pages;
}

static bool cxlswap_is_full(void)
{
	return totalexmem_pages() * cxlswap_max_pool_percent / 100 <
				DIV_ROUND_UP(cxlswap_pool_total_size, PAGE_SIZE);
}

static bool cxlswap_can_accept(void)
{
	return totalexmem_pages() * cxlswap_accept_thr_percent / 100 *
				cxlswap_max_pool_percent / 100 >
			DIV_ROUND_UP(cxlswap_pool_total_size, PAGE_SIZE);
}

static void cxlswap_update_total_size(void)
{
	struct cxlswap_pool *pool;
	u64 total = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(pool, &cxlswap_pools, list)
		total += cxlpool_get_total_size(pool->cxlpool);
	rcu_read_unlock();

	// Byte
	cxlswap_pool_total_size = total;
}

/*
 * cxlswap entry functions
 */

static struct kmem_cache *cxlswap_entry_cache;

static int __init cxlswap_entry_cache_create(void)
{
	cxlswap_entry_cache = KMEM_CACHE(cxlswap_entry, 0);
	return cxlswap_entry_cache == NULL;
}

static void __init cxlswap_entry_cache_destroy(void)
{
	kmem_cache_destroy(cxlswap_entry_cache);
}

static struct cxlswap_entry *cxlswap_entry_cache_alloc(gfp_t gfp)
{
	struct cxlswap_entry *entry;

	entry = kmem_cache_alloc(cxlswap_entry_cache, gfp);
	if (!entry)
		return NULL;
	entry->refcount = 1;
	RB_CLEAR_NODE(&entry->rbnode);
	return entry;
}

static void cxlswap_entry_cache_free(struct cxlswap_entry *entry)
{
	kmem_cache_free(cxlswap_entry_cache, entry);
}

/*
 * rbtree functions
 */

static struct cxlswap_entry *cxlswap_rb_search(struct rb_root *root,
				pgoff_t offset)
{
	struct rb_node *node = root->rb_node;
	struct cxlswap_entry *entry;

	while (node) {
		entry = rb_entry(node, struct cxlswap_entry, rbnode);
		if (entry->offset > offset)
			node = node->rb_left;
		else if (entry->offset < offset)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

static int cxlswap_rb_insert(struct rb_root *root, struct cxlswap_entry *entry,
				struct cxlswap_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct cxlswap_entry *myentry;

	while (*link) {
		parent = *link;
		myentry = rb_entry(parent, struct cxlswap_entry, rbnode);
		if (myentry->offset > entry->offset)
			link = &(*link)->rb_left;
		else if (myentry->offset < entry->offset)
			link = &(*link)->rb_right;
		else {
			*dupentry = myentry;
			return -EEXIST;
		}
	}
	rb_link_node(&entry->rbnode, parent, link);
	rb_insert_color(&entry->rbnode, root);
	return 0;
}

static void cxlswap_rb_erase(struct rb_root *root, struct cxlswap_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

static void cxlswap_free_entry(struct cxlswap_entry *entry)
{
	if (!entry->length)
		atomic_dec(&cxlswap_same_filled_pages);
	else {
		cxlpool_free(entry->pool->cxlpool, entry->handle);
		cxlswap_pool_put(entry->pool);
	}
	cxlswap_entry_cache_free(entry);
	atomic_dec(&cxlswap_stored_pages);
	cxlswap_update_total_size();
}

static void cxlswap_entry_get(struct cxlswap_entry *entry)
{
	entry->refcount++;
}

static void cxlswap_entry_put(struct cxlswap_tree *tree,
				struct cxlswap_entry *entry)
{
	int refcount = --entry->refcount;

	BUG_ON(refcount < 0);
	if (refcount == 0) {
		cxlswap_rb_erase(&tree->rbroot, entry);
		cxlswap_free_entry(entry);
	}
}

static struct cxlswap_entry *cxlswap_entry_find_get(struct rb_root *root,
				pgoff_t offset)
{
	struct cxlswap_entry *entry = NULL;

	entry = cxlswap_rb_search(root, offset);
	if (entry)
		cxlswap_entry_get(entry);

	return entry;
}

/*
 * pool functions
 */

static struct cxlswap_pool *__cxlswap_pool_current(void)
{
	struct cxlswap_pool *pool;

	pool = list_first_or_null_rcu(&cxlswap_pools, typeof(*pool), list);
	WARN_ONCE(!pool && cxlswap_has_pool,
				"%s: no page storage pool!\n", __func__);

	return pool;
}
static struct cxlswap_pool *cxlswap_pool_current(void)
{
	assert_spin_locked(&cxlswap_pools_lock);

	return __cxlswap_pool_current();
}

static struct cxlswap_pool *cxlswap_pool_current_get(void)
{
	struct cxlswap_pool *pool;

	rcu_read_lock();

	pool = __cxlswap_pool_current();
	if (!cxlswap_pool_get(pool))
		pool = NULL;

	rcu_read_unlock();

	return pool;
}

static struct cxlswap_pool *cxlswap_pool_last_get(void)
{
	struct cxlswap_pool *pool, *last = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(pool, &cxlswap_pools, list)
		last = pool;
	WARN_ONCE(!last && cxlswap_has_pool,
		"%s: no page storage pool!\n", __func__);
	if (!cxlswap_pool_get(last))
		last = NULL;

	rcu_read_unlock();

	return last;
}

static void shrink_worker(struct work_struct *w)
{
	struct cxlswap_pool *pool = container_of(w, typeof(*pool), shrink_work);

	if (cxlpool_shrink(pool->cxlpool, 1, NULL, false))
		cxlswap_reject_reclaim_fail++;
	cxlswap_pool_put(pool);
}

static void flush_worker(struct work_struct *w)
{
	struct cxlswap_pool *pool = container_of(w, typeof(*pool), flush_work);

	u64 pages = cxlpool_get_total_size(pool->cxlpool) / PAGE_SIZE;

	cxlpool_shrink(pool->cxlpool, pages, NULL, true);
	cxlswap_pool_put(pool);

	pages = cxlpool_get_total_size(pool->cxlpool) / PAGE_SIZE;
	if (pages != 0)
		VM_WARN_ON_ONCE(1);

	complete(&pool->done);
}

static struct cxlswap_pool *cxlswap_pool_create(char *type)
{
	struct cxlswap_pool *pool;
	char name[40]; /* 'cxlswap' + 32 char (max) num + \0 */
	gfp_t gfp = __GFP_NORETRY | __GFP_NOWARN | __GFP_KSWAPD_RECLAIM;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	snprintf(name, 40, "cxlswap%x", atomic_inc_return(&cxlswap_pools_count));
	pool->cxlpool = cxlpool_create_pool(type, name, gfp, &cxlswap_cxlpool_ops);
	if (!pool->cxlpool) {
		pr_err("%s cxlpool not available\n", type);
		goto error;
	}

	kref_init(&pool->kref);
	INIT_LIST_HEAD(&pool->list);
	INIT_WORK(&pool->shrink_work, shrink_worker);
	INIT_WORK(&pool->flush_work, flush_worker);

	return pool;

error:
	kfree(pool);
	return NULL;
}

static __init struct cxlswap_pool *__cxlswap_pool_create_fallback(void)
{
	/* In this situation, this is only simple wrapping function */

	/*
	 * TODO - Does we support a number of pools(e.g. movable or not...)?
	 * If then, we fill this function in the future.
	 * If not, remove this function will be okay.
	 */
	return cxlswap_pool_create(cxlswap_cxlpool_type);
}

static void cxlswap_pool_destroy(struct cxlswap_pool *pool)
{
	pr_debug("cxlswap destroying pool");

	cxlpool_destroy_pool(pool->cxlpool);
	kfree(pool);
}

static int __must_check cxlswap_pool_get(struct cxlswap_pool *pool)
{
	if (!pool)
		return 0;

	return kref_get_unless_zero(&pool->kref);
}

static void __cxlswap_pool_release(struct work_struct *work)
{
	struct cxlswap_pool *pool = container_of(work, typeof(*pool), release_work);

	synchronize_rcu();

	/* nobody should have been able to get a kref... */
	WARN_ON(kref_get_unless_zero(&pool->kref));

	/* pool is now off cxlswap_pools list and has no references. */
	cxlswap_pool_destroy(pool);
}

static void __cxlswap_pool_empty(struct kref *kref)
{
	struct cxlswap_pool *pool;

	pool = container_of(kref, typeof(*pool), kref);

	spin_lock(&cxlswap_pools_lock);

	WARN_ON(pool == cxlswap_pool_current());

	list_del_rcu(&pool->list);

	INIT_WORK(&pool->release_work, __cxlswap_pool_release);
	schedule_work(&pool->release_work);

	spin_unlock(&cxlswap_pools_lock);
}

static void cxlswap_pool_put(struct cxlswap_pool *pool)
{
	kref_put(&pool->kref, __cxlswap_pool_empty);
}

/*
 * param callbacks
 */

static int __cxlswap_param_set(const char *val, const struct kernel_param *kp,
				char *type)
{
	char *s = strstrip((char *)val);

	if (cxlswap_init_failed) {
		pr_err("can't set param, initialization failed\n");
		return -ENODEV;
	}

	/* no change required */
	if (!strcmp(s, *(char **)kp->arg) && cxlswap_has_pool)
		return 0;

	/* If we have many cxlswap pool later, fill this function at that time. */
	return -EINVAL;
}

static int cxlswap_cxlpool_param_set(const char *val,
				const struct kernel_param *kp)
{
	return __cxlswap_param_set(val, kp, NULL);
}

static int cxlswap_enabled_param_set(const char *val,
				const struct kernel_param *kp)
{
	if (cxlswap_init_failed) {
		pr_err("can't enable, initialization failed\n");
		return -ENODEV;
	}
	if (!cxlswap_has_pool && cxlswap_init_started) {
		pr_err("can't enable, no pool configured\n");
		return -ENODEV;
	}

	return param_set_bool(val, kp);
}

static int cxlswap_flush_param_set(const char *val,
				const struct kernel_param *kp)
{
	struct cxlswap_pool *pool;

	if (cxlswap_init_failed) {
		pr_err("can't enable, initialization failed\n");
		return -ENODEV;
	}
	if (!cxlswap_has_pool && cxlswap_init_started) {
		pr_err("can't enable, no pool configured\n");
		return -ENODEV;
	}
	if (cxlswap_enabled) {
		pr_err("Flush is supported only when CXL Swap is disabled\n");
		return -EPERM;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(pool, &cxlswap_pools, list) 
		if (cxlswap_pool_get(pool)) {
			init_completion(&pool->done);
			queue_work(flush_wq, &pool->flush_work);
			wait_for_completion(&pool->done);
		}
	rcu_read_unlock();

	return 0;
}

/*
 * writeback code
 */

/* return enum for cxlswap_get_swap_cache_page */
enum cxlswap_get_swap_ret {
	CXLSWAP_SWAPCACHE_NEW,
	CXLSWAP_SWAPCACHE_EXIST,
	CXLSWAP_SWAPCACHE_FAIL,
};

static int cxlswap_get_swap_cache_page(swp_entry_t entry,
				struct page **retpage)
{
	bool page_was_allocated;
	struct mempolicy *mpol;
	struct folio *folio;

	mpol = get_task_policy(current);
	folio = __read_swap_cache_async(entry, GFP_KERNEL, mpol,
				NO_INTERLEAVE_INDEX, &page_was_allocated);
	*retpage = &folio->page;

	if (page_was_allocated)
		return CXLSWAP_SWAPCACHE_NEW;
	if (!*retpage)
		return CXLSWAP_SWAPCACHE_FAIL;
	return CXLSWAP_SWAPCACHE_EXIST;
}

static int cxlswap_writeback_entry(struct cxlpool *pool, unsigned long handle,
				bool under_flush)
{
	struct cxlswap_header *chdr;
	swp_entry_t swpentry;
	struct cxlswap_tree *tree;
	pgoff_t offset;
	struct cxlswap_entry *entry;
	struct page *page, *src_page;

	u8 *src, *dst;
	int ret;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
	};

	chdr = cxlpool_map_handle(pool, handle, CXLPOOL_MM_RO);

	/* Get swpentry key for disk swap out from page->private */
	src_page = virt_to_page(chdr);
	swpentry.val = src_page->private;

	tree = cxlswap_trees[swp_type(swpentry)];
	offset = swp_offset(swpentry);

	/* find and ref cxlswap entry */
	spin_lock(&tree->lock);
	entry = cxlswap_entry_find_get(&tree->rbroot, offset);
	if (!entry) {
		/* entry was invalidated */
		spin_unlock(&tree->lock);
		cxlpool_unmap_handle(pool, handle);
		return 0;
	}
	spin_unlock(&tree->lock);
	BUG_ON(offset != entry->offset);

	src = (u8 *)chdr;

	/* try to allocate swap cache page */
	switch (cxlswap_get_swap_cache_page(swpentry, &page)) {
	case CXLSWAP_SWAPCACHE_FAIL: /* no memory or invalidate happened */
		if (under_flush && !__swap_count(swpentry)) 
			goto flush;

		ret = -ENOMEM;
		goto fail;

	case CXLSWAP_SWAPCACHE_EXIST:
		/* page is already in the swap cache, ignore for now */
		put_page(page);
		if (under_flush && folio_trylock(page_folio(page))) {
			get_page(page);
			break;
		}

		ret = -EEXIST;
		goto fail;

	case CXLSWAP_SWAPCACHE_NEW: /* page is locked */
		/* Nothing to do. Just set page to uptodate */
		dst = page_address(page);
		memcpy(dst, src, PAGE_SIZE);
		ret = 0;

		/* page is up to date */
		SetPageUptodate(page);
	}

	/* move it to the tail of the inactive list after end_writeback */
	SetPageReclaim(page);

	/* start writeback */
	__swap_writepage(page_folio(page), &wbc);
	put_page(page);
	cxlswap_written_back_pages++;

	spin_lock(&tree->lock);
	/* drop local reference */
	cxlswap_entry_put(tree, entry);

	if (entry == cxlswap_rb_search(&tree->rbroot, offset))
		cxlswap_entry_put(tree, entry);
	spin_unlock(&tree->lock);

	goto end;

flush:
	spin_lock(&tree->lock);
	cxlswap_rb_erase(&tree->rbroot, entry);
	cxlswap_entry_put(tree, entry); 
	spin_unlock(&tree->lock);
	ret = 0;
			
fail:
	spin_lock(&tree->lock);
	cxlswap_entry_put(tree, entry);
	spin_unlock(&tree->lock);

end:
	cxlpool_unmap_handle(pool, handle);

	return ret;
}

static int cxlswap_is_page_same_filled(void *ptr, unsigned long *value)
{
	unsigned int pos;
	unsigned long *page;

	page = (unsigned long *)ptr;
	for (pos = 1; pos < PAGE_SIZE / sizeof(*page); pos++) {
		if (page[pos] != page[0])
			return 0;
	}
	*value = page[0];
	return 1;
}

static void cxlswap_fill_page(void *ptr, unsigned long value)
{
	unsigned long *page;

	page = (unsigned long *)ptr;
	memset_l(page, value, PAGE_SIZE / sizeof(unsigned long));
}

/*
 * frontswap hooks
 */

/* attempts to store an single page */
static int cxlswap_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
	struct cxlswap_tree *tree = cxlswap_trees[type];
	struct cxlswap_entry *entry, *dupentry;
	struct cxlswap_pool *pool;
	unsigned long handle, value;
	int ret = -ENOMEM;
	struct page *dst_page;
	char *buf;
	u8 *src;
	struct cxlswap_header chdr = { .swpentry = swp_entry(type, offset) };
	gfp_t gfp;

	/* Swap out from CXL memory isn't supported */
	if (zone_has_subzone(page_zone(page))) {
		ret = -ENOTSUPP;
		goto reject;
	}

	/* THP isn't supported */
	if (PageTransHuge(page)) {
		ret = -EINVAL;
		goto reject;
	}

	if (!cxlswap_enabled || !tree) {
		ret = -ENODEV;
		goto reject;
	}

	/* reclaim space if needed */
	if (cxlswap_is_full()) {
		cxlswap_pool_limit_hit++;
		cxlswap_pool_reached_full = true;
		goto shrink;
	}

	if (cxlswap_pool_reached_full) {
		if (!cxlswap_can_accept()) {
			ret = -ENOMEM;
			goto reject;
		} else
			cxlswap_pool_reached_full = false;
	}

	/* allocate entry */
	entry = cxlswap_entry_cache_alloc(GFP_KERNEL);
	if (!entry) {
		cxlswap_reject_kmemcache_fail++;
		ret = -ENOMEM;
		goto reject;
	}

	if (cxlswap_same_filled_pages_enabled) {
		src = kmap_atomic(page);
		if (cxlswap_is_page_same_filled(src, &value)) {
			kunmap_atomic(src);
			entry->offset = offset;
			entry->length = 0;
			entry->value = value;
			atomic_inc(&cxlswap_same_filled_pages);
			goto insert_entry;
		}
		kunmap_atomic(src);
	}

	if (!cxlswap_non_same_filled_pages_enabled) {
		ret = -EINVAL;
		goto freepage;
	}

	/* if entry is successfully added, it keeps the reference */
	entry->pool = cxlswap_pool_current_get();
	if (!entry->pool) {
		ret = -EINVAL;
		goto freepage;
	}

	/* store */
	gfp = __GFP_NORETRY | __GFP_NOWARN | __GFP_KSWAPD_RECLAIM | GFP_HIGHUSER_MOVABLE;
	ret = cxlpool_malloc(entry->pool->cxlpool, PAGE_SIZE, gfp, &handle);
	if (ret) {
		cxlswap_reject_alloc_fail++;
		goto put_dstmem;
	}

	buf = cxlpool_map_handle(entry->pool->cxlpool, handle, CXLPOOL_MM_WO);

	dst_page = virt_to_page(handle);
	dst_page->private = chdr.swpentry.val;

	src = kmap_atomic(page);

	memcpy(buf, src, PAGE_SIZE);

	kunmap_atomic(src);
	cxlpool_unmap_handle(entry->pool->cxlpool, handle);

	/* populate entry */
	entry->offset = offset;
	entry->handle = handle;
	entry->length = PAGE_SIZE;

insert_entry:
	/* map */
	spin_lock(&tree->lock);
	do {
		ret = cxlswap_rb_insert(&tree->rbroot, entry, &dupentry);
		if (ret == -EEXIST) {
			/* remove from rbtree */
			cxlswap_rb_erase(&tree->rbroot, dupentry);
			cxlswap_entry_put(tree, dupentry);
		}
	} while (ret == -EEXIST);
	spin_unlock(&tree->lock);

	/* update stats */
	atomic_inc(&cxlswap_stored_pages);
	cxlswap_update_total_size();
	count_vm_event(CXLSWPOUT);

	return 0;

put_dstmem:
	cxlswap_pool_put(entry->pool);
freepage:
	cxlswap_entry_cache_free(entry);
reject:
	return ret;

shrink:
	pool = cxlswap_pool_last_get();
	if (pool)
		queue_work(shrink_wq, &pool->shrink_work);
	ret = -ENOMEM;
	goto reject;
}

/*
 * returns 0 if the page was successfully processed
 * return -1 on entry not found or error
 * TODO exclusive
*/
static int cxlswap_frontswap_load(unsigned type, pgoff_t offset,
				struct page *page, bool *exclusive)
{
	struct cxlswap_tree *tree = cxlswap_trees[type];
	struct cxlswap_entry *entry;
	u8 *src, *dst;

	/* find */
	spin_lock(&tree->lock);
	entry = cxlswap_entry_find_get(&tree->rbroot, offset);
	if (!entry) {
		/* entry was written back */
		spin_unlock(&tree->lock);
		return -1;
	}
	spin_unlock(&tree->lock);

	if (!entry->length) {
		dst = kmap_atomic(page);
		cxlswap_fill_page(dst, entry->value);
		kunmap_atomic(dst);
		goto stats;
	}

	src = cxlpool_map_handle(entry->pool->cxlpool, entry->handle,
			CXLPOOL_MM_RO);
	dst = kmap_atomic(page);

	memcpy(dst, src, entry->length);

	kunmap_atomic(dst);
	cxlpool_unmap_handle(entry->pool->cxlpool, entry->handle);
stats:
	count_vm_event(CXLSWPIN);

	spin_lock(&tree->lock);
	cxlswap_entry_put(tree, entry);
	spin_unlock(&tree->lock);

	return 0;
}

/* frees an entry in cxlswap */
static void cxlswap_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct cxlswap_tree *tree = cxlswap_trees[type];
	struct cxlswap_entry *entry;

	/* find */
	spin_lock(&tree->lock);
	entry = cxlswap_rb_search(&tree->rbroot, offset);
	if (!entry) {
		/* entry was written back */
		spin_unlock(&tree->lock);
		return;
	}

	/* remove from rbtree */
	cxlswap_rb_erase(&tree->rbroot, entry);

	/* drop the initial reference from entry creation */
	cxlswap_entry_put(tree, entry);

	spin_unlock(&tree->lock);
}

/* frees all cxlswap entries for the given swap type */
static void cxlswap_frontswap_invalidate_area(unsigned type)
{
	struct cxlswap_tree *tree = cxlswap_trees[type];
	struct cxlswap_entry *entry, *n;

	if (!tree)
		return;

	/* walk the tree and free everything */
	spin_lock(&tree->lock);
	rbtree_postorder_for_each_entry_safe(entry, n, &tree->rbroot, rbnode)
		cxlswap_free_entry(entry);
	tree->rbroot = RB_ROOT;
	spin_unlock(&tree->lock);
	kfree(tree);
	cxlswap_trees[type] = NULL;
}

static void cxlswap_frontswap_init(unsigned type)
{
	struct cxlswap_tree *tree;

	tree = kzalloc(sizeof(*tree), GFP_KERNEL);
	if (!tree) {
		pr_err("cxlswap tree alloc failed, disabled for swap type %d\n", type);
		return;
	}

	tree->rbroot = RB_ROOT;
	spin_lock_init(&tree->lock);
	cxlswap_trees[type] = tree;
}

static struct frontswap_ops cxlswap_frontswap_ops = {
	.store = cxlswap_frontswap_store,
	.load = cxlswap_frontswap_load,
	.invalidate_page = cxlswap_frontswap_invalidate_page,
	.invalidate_area = cxlswap_frontswap_invalidate_area,
	.init = cxlswap_frontswap_init
};

/*
 * debugfs functions
 */
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *cxlswap_debugfs_root;

static int __init cxlswap_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	cxlswap_debugfs_root = debugfs_create_dir("cxlswap", NULL);

	debugfs_create_u64("pool_limit_hit", 0444,
				cxlswap_debugfs_root, &cxlswap_pool_limit_hit);
	debugfs_create_u64("reject_reclaim_fail", 0444,
				cxlswap_debugfs_root, &cxlswap_reject_reclaim_fail);
	debugfs_create_u64("reject_alloc_fail", 0444,
				cxlswap_debugfs_root, &cxlswap_reject_alloc_fail);
	debugfs_create_u64("reject_kmemcache_fail", 0444,
				cxlswap_debugfs_root, &cxlswap_reject_kmemcache_fail);
	debugfs_create_u64("written_back_pages", 0444,
				cxlswap_debugfs_root, &cxlswap_written_back_pages);
	debugfs_create_u64("pool_total_size", 0444,
				cxlswap_debugfs_root, &cxlswap_pool_total_size);
	debugfs_create_atomic_t("stored_pages", 0444,
				cxlswap_debugfs_root, &cxlswap_stored_pages);
	debugfs_create_atomic_t("same_filled_pages", 0444,
				cxlswap_debugfs_root, &cxlswap_same_filled_pages);

	return 0;
}
#else
static int __init cxlswap_debugfs_init(void)
{
	return 0;
}
#endif

/*
 * module init and exit
 */
static int __init init_cxlswap(void)
{
	struct cxlswap_pool *pool;

	cxlswap_init_started = true;

	if (cxlswap_entry_cache_create()) {
		pr_err("cxlswap entry cache creation failed\n");
		goto cache_fail;
	}

	pool = __cxlswap_pool_create_fallback();
	if (pool) {
		pr_info("cxlswap using pool (allocator: %s)\n",
				cxlpool_get_type(pool->cxlpool));
		list_add(&pool->list, &cxlswap_pools);
		cxlswap_has_pool = true;
	} else {
		pr_err("cxlswap pool creation failed\n");
		cxlswap_enabled = false;
	}

	shrink_wq = create_workqueue("cxlswap-shrink");
	if (!shrink_wq)
		goto create_wq_fail;

	flush_wq = create_workqueue("cxlswap-flush");
	if (!flush_wq)
		goto destroy_shrink_wq;

	frontswap_register_ops(&cxlswap_frontswap_ops);
	if (cxlswap_debugfs_init())
		pr_warn("debugfs_initialization failed\n");

	return 0;

destroy_shrink_wq:
	destroy_workqueue(shrink_wq);
create_wq_fail:
	if (pool)
		cxlswap_pool_destroy(pool);

	cxlswap_entry_cache_destroy();
cache_fail:
	cxlswap_init_failed = true;
	cxlswap_enabled = false;
	return -ENOMEM;
}
/* TODO
 * Do we should call late_initcall? we don't need to wait crypto warm-up time
 */
late_initcall(init_cxlswap);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Seungjun Ha <seungjun.ha@samsung.com>");
MODULE_DESCRIPTION("Non-Compressed frontswap backend for CXL DRAM Device");
