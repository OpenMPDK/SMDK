#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/cleancache.h>
#include <linux/rbtree.h>
#include <linux/radix-tree.h>
#include <linux/cxlpool.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/workqueue.h>
#include <linux/highmem.h>

#define MAX_CXLCACHE_POOL 2048

/*
 * statistics
 */

static u64 cxlcache_pool_total_size;

static atomic_t cxlcache_put_pages = ATOMIC_INIT(0);

static u64 cxlcache_pool_limit_hit;

static u64 cxlcache_evicted_pages;

static u64 cxlcache_reject_reclaim_fail;

static u64 cxlcache_reject_alloc_fail;

static u64 cxlcache_reject_kmemcache_fail;

static struct workqueue_struct *shrink_wq;
static struct workqueue_struct *flush_wq;

static bool cxlcache_pool_reached_full;

/*
 * tunables
 */

static bool cxlcache_enabled = IS_ENABLED(CONFIG_CXLCACHE_DEFAULT_ON);
static int cxlcache_enabled_param_set(const char *,
		const struct kernel_param *);
static const struct kernel_param_ops cxlcache_enabled_param_ops = {
	.set =		cxlcache_enabled_param_set,
	.get =		param_get_bool,
};
module_param_cb(enabled, &cxlcache_enabled_param_ops, &cxlcache_enabled, 0644);

static char *cxlcache_cxlpool_type = CONFIG_CXLCACHE_CXLPOOL_DEFAULT;
static int cxlcache_cxlpool_param_set(const char *, const struct kernel_param *);
static const struct kernel_param_ops cxlcache_cxlpool_param_ops = {
	.set =		cxlcache_cxlpool_param_set,
	.get =		param_get_charp,
	.free =		param_free_charp,
};
module_param_cb(cxlpool, &cxlcache_cxlpool_param_ops,
		&cxlcache_cxlpool_type, 0644);

static unsigned int cxlcache_max_pool_percent = 20;
module_param_named(max_pool_percent, cxlcache_max_pool_percent, uint, 0644);

static unsigned int cxlcache_accept_thr_percent = 90;
module_param_named(accept_threshold_percent, cxlcache_accept_thr_percent,
		uint, 0644);

static bool cxlcache_flush;
static int cxlcache_flush_param_set(const char *, const struct kernel_param *);
static const struct kernel_param_ops cxlcache_flush_param_ops = {
	.set = 		cxlcache_flush_param_set,
	.get = 		NULL,
};
module_param_cb(flush, &cxlcache_flush_param_ops, &cxlcache_flush, 0200);

/*
 * data structures
 */

struct cxlcache_pool {
	struct cxlpool *cxlpool;
	struct kref kref;
	struct list_head list;
	struct work_struct release_work;
	struct work_struct shrink_work;
	struct work_struct flush_work;
	struct hlist_node node;
	struct completion done;
	int pagesize;
};

struct cxlcache_inode {
	struct cleancache_filekey filekey;
	struct rb_node rb_tree_node; /* Protected by cxlcache_tree->lock */
	long cc_page_count; /* Atomicity depends on cc_inode_lock. */
	struct radix_tree_root tree_root; /* Tree of pages within inode. */
	struct cxlcache_pool *pool;
	spinlock_t cc_inode_lock;
};

struct cxlcache_page {
	struct cxlcache_inode *cc_inode;
	int size;
	int index;
	union {
		unsigned long handle;
		unsigned long value;
	};
	struct cxlcache_pool *pool;
	spinlock_t cc_page_lock;
};

struct cxlcache_header {
	struct cleancache_filekey filekey;
	int index;
	int pool_id;
};

struct cxlcache_tree {
	struct rb_root rbroot;
	rwlock_t lock;
};

/* TODO - array Need? */
static struct cxlcache_tree *cxlcache_trees[MAX_CXLCACHE_POOL];

spinlock_t cxlcache_tree_lock;

static LIST_HEAD(cxlcache_pools);

static DEFINE_SPINLOCK(cxlcache_pools_lock);

static atomic_t cxlcache_pools_count = ATOMIC_INIT(0);

static bool cxlcache_init_failed;

static bool cxlcache_has_pool;

/*
 * helpers and fwd declarations
 */

static int cxlcache_evict(struct cxlpool *pool, unsigned long handle,
		bool under_flush); 
static int cxlcache_pool_get(struct cxlcache_pool *pool);
static void cxlcache_pool_put(struct cxlcache_pool *pool);

static const struct cxlpool_ops cxlcache_cxlpool_ops = {
	.evict = cxlcache_evict
};

static void cxlcache_invalidate_page(int pool_id, struct cleancache_filekey key,
		pgoff_t index);

/* Get total number of pages on ZONE_EXMEM */
static inline unsigned long totalexmem_pages(void)
{
	unsigned long _totalexmem_pages = 0;
	int nid;

	for_each_online_node(nid) {
		struct zone *zone = &NODE_DATA(nid)->node_zones[ZONE_EXMEM];
		_totalexmem_pages += zone_managed_pages(zone);
	}

	return _totalexmem_pages;
}

static bool cxlcache_is_full(void)
{
	return totalexmem_pages() * cxlcache_max_pool_percent / 100 <
		DIV_ROUND_UP(cxlcache_pool_total_size, PAGE_SIZE);
}

static bool cxlcache_can_accept(void)
{
	return totalexmem_pages() * cxlcache_accept_thr_percent / 100 *
		cxlcache_max_pool_percent / 100 >
		DIV_ROUND_UP(cxlcache_pool_total_size, PAGE_SIZE);
}

static void cxlcache_update_total_size(void)
{
	struct cxlcache_pool *pool;
	u64 total = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(pool, &cxlcache_pools, list)
		total += cxlpool_get_total_size(pool->cxlpool);
	rcu_read_unlock();

	// Byte
	cxlcache_pool_total_size = total;
}

/*
 * cxlcache metadata allocation functions
 */

static struct kmem_cache *cxlcache_inode_cache;
static struct kmem_cache *cxlcache_page_cache;
static struct kmem_cache *cxlcache_header_cache;

static int __init cxlcache_inode_cache_create(void)
{
	cxlcache_inode_cache = KMEM_CACHE(cxlcache_inode, 0);
	return cxlcache_inode_cache == NULL;
}

static int __init cxlcache_page_cache_create(void)
{
	cxlcache_page_cache = KMEM_CACHE(cxlcache_page, 0);
	return cxlcache_page_cache == NULL;
}

static int __init cxlcache_header_cache_create(void)
{
	cxlcache_header_cache = KMEM_CACHE(cxlcache_header, 0);
	return cxlcache_header_cache == NULL;
}

static void __init cxlcache_inode_cache_destroy(void)
{
	kmem_cache_destroy(cxlcache_inode_cache);
}

static void __init cxlcache_page_cache_destroy(void)
{
	kmem_cache_destroy(cxlcache_page_cache);
}

static void __init cxlcache_header_cache_destroy(void)
{
	kmem_cache_destroy(cxlcache_header_cache);
}

static struct cxlcache_inode *cxlcache_inode_cache_alloc(gfp_t gfp,
		struct cleancache_filekey key)
{
	struct cxlcache_inode *cc_inode;

	cc_inode = kmem_cache_alloc(cxlcache_inode_cache, gfp);
	if (!cc_inode)
		return NULL;
	INIT_RADIX_TREE(&cc_inode->tree_root, GFP_KERNEL);
	spin_lock_init(&cc_inode->cc_inode_lock);
	cc_inode->filekey = key;
	cc_inode->cc_page_count = 0;
	cc_inode->pool = NULL;
	RB_CLEAR_NODE(&cc_inode->rb_tree_node);
	return cc_inode;
}

static struct cxlcache_page *cxlcache_page_cache_alloc(gfp_t gfp,
		struct cxlcache_inode *cc_inode)
{
	struct cxlcache_page *cc_page;

	cc_page = kmem_cache_alloc(cxlcache_page_cache, gfp);
	if (!cc_page)
		return NULL;
	spin_lock_init(&cc_page->cc_page_lock);
	cc_page->cc_inode = cc_inode;
	cc_page->index = -1;
	cc_page->size = 0;
	cc_page->handle = -1;
	cc_page->pool = NULL;
	return cc_page;
}

static struct cxlcache_header *cxlcache_header_cache_alloc(gfp_t gfp)
{
	struct cxlcache_header *chdr;
	struct cleancache_filekey key = { .u.key = { 0 } };

	chdr = kmem_cache_alloc(cxlcache_header_cache, gfp);
	if (!chdr)
		return NULL;
	chdr->filekey = key;
	chdr->index = -1;
	chdr->pool_id = -1;
	return chdr;
}

static void cxlcache_inode_cache_free(struct cxlcache_inode *cc_inode)
{
	kmem_cache_free(cxlcache_inode_cache, cc_inode);
}

static void cxlcache_page_cache_free(struct cxlcache_page *cc_page)
{
	kmem_cache_free(cxlcache_page_cache, cc_page);
}

static void cxlcache_header_cache_free(struct cxlcache_header *chdr)
{
	kmem_cache_free(cxlcache_header_cache, chdr);
}

/*
 * rbtree functions. Related with cleancache_inode
 */

/* Return found cxlcache_inode */
static struct cxlcache_inode *cxlcache_inode_search(struct cxlcache_tree *tree,
		struct cleancache_filekey filekey)
{
	struct rb_node *node;
	struct cxlcache_inode *cc_inode;

restart_find:
	read_lock(&tree->lock);
	node = tree->rbroot.rb_node;
	while (node) {
		cc_inode = rb_entry(node, struct cxlcache_inode, rb_tree_node);
		if (cc_inode->filekey.u.ino > filekey.u.ino)
			node = node->rb_left;
		else if (cc_inode->filekey.u.ino < filekey.u.ino)
			node = node->rb_right;
		else {
			if (!spin_trylock(&cc_inode->cc_inode_lock)) {
				read_unlock(&tree->lock);
				goto restart_find;
			}
			read_unlock(&tree->lock);
			return cc_inode;
		}
	}
	read_unlock(&tree->lock);
	return NULL;
}

static int cxlcache_inode_insert(struct rb_root *root, struct cxlcache_inode *cc_inode)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct cxlcache_inode *this;

	while (*link) {
		parent = *link;
		this = container_of(parent, struct cxlcache_inode, rb_tree_node);
		if (this->filekey.u.ino > cc_inode->filekey.u.ino)
			link = &(*link)->rb_left;
		else if (this->filekey.u.ino < cc_inode->filekey.u.ino)
			link = &(*link)->rb_right;
		else
			return 0;
	}
	rb_link_node(&cc_inode->rb_tree_node, parent, link);
	rb_insert_color(&cc_inode->rb_tree_node, root);
	return 1;
}

static void cxlcache_inode_erase(struct rb_root *root, struct cxlcache_inode *cc_inode)
{
	if (!RB_EMPTY_NODE(&cc_inode->rb_tree_node)) {
		rb_erase(&cc_inode->rb_tree_node, root);
		RB_CLEAR_NODE(&cc_inode->rb_tree_node);
	}
}

static void cxlcache_inode_free(struct cxlcache_tree *tree, struct cxlcache_inode *cc_inode)
{
	BUG_ON(!spin_is_locked(&cc_inode->cc_inode_lock));
	BUG_ON(cc_inode == NULL);
	BUG_ON(cc_inode->cc_page_count != 0);
	BUG_ON(tree == NULL);

	write_lock(&tree->lock);
	cxlcache_inode_erase(&tree->rbroot, cc_inode);
	cxlcache_pool_put(cc_inode->pool);
	spin_unlock(&cc_inode->cc_inode_lock);
	cxlcache_inode_cache_free(cc_inode);
	write_unlock(&tree->lock);
}

/* Radix tree functions. Related with cxlcache_page. */
static struct cxlcache_page *cxlcache_page_lookup_in_cc_inode(struct cxlcache_inode *cc_inode,
		int index)
{
	struct cxlcache_page *cc_page = NULL;
	BUG_ON(cc_inode == NULL);
	BUG_ON(!spin_is_locked(&cc_inode->cc_inode_lock));

	cc_page = radix_tree_lookup(&cc_inode->tree_root, index);
	if (cc_page != NULL) {
		spin_lock(&cc_page->cc_page_lock);
		spin_unlock(&cc_inode->cc_inode_lock);
	}

	return cc_page;
}

static int cxlcache_page_add_to_cc_inode(struct cxlcache_inode *cc_inode, int index, 
		struct cxlcache_page *cc_page)
{
	int ret;

	BUG_ON(!spin_is_locked(&cc_inode->cc_inode_lock));
	BUG_ON(!spin_is_locked(&cc_page->cc_page_lock));

	ret = radix_tree_insert(&cc_inode->tree_root, index, cc_page);
	if (!ret)
		cc_inode->cc_page_count++;
	return ret;
}

void cxlcache_page_delete_from_cc_inode(struct cxlcache_inode *cc_inode,
		struct cxlcache_page *cc_page)
{
	BUG_ON(cc_inode == NULL);
	BUG_ON(!spin_is_locked(&cc_inode->cc_inode_lock));
	BUG_ON(!spin_is_locked(&cc_page->cc_page_lock));

	if (cc_page == radix_tree_delete(&cc_inode->tree_root, cc_page->index))
		cc_inode->cc_page_count--;
	BUG_ON(cc_inode->cc_page_count < 0);
}

static struct cxlcache_page *cxlcache_index_delete_from_cc_inode(struct cxlcache_inode *cc_inode,
		pgoff_t index)
{
	struct cxlcache_page *cc_page = NULL;

	BUG_ON(cc_inode == NULL);
	BUG_ON(!spin_is_locked(&cc_inode->cc_inode_lock));

	cc_page = radix_tree_lookup(&cc_inode->tree_root, index);
	if (cc_page != NULL) {
		spin_lock(&cc_page->cc_page_lock);
		if (cc_page == radix_tree_delete(&cc_inode->tree_root, index))
			cc_inode->cc_page_count--;
	}
	BUG_ON(cc_inode->cc_page_count < 0);
	return cc_page;
}

static void cxlcache_page_free_data(struct cxlcache_page *cc_page)
{
	cxlpool_free(cc_page->pool->cxlpool, cc_page->handle);
}


/* destory cc_inode traversing radix tree */

static void cxlcache_inode_destroy(struct cxlcache_tree *tree, struct cxlcache_inode *cc_inode)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	BUG_ON(!spin_is_locked(&cc_inode->cc_inode_lock));

	radix_tree_for_each_slot (slot, &cc_inode->tree_root, &iter, 0) {
		struct cxlcache_page *cc_page = radix_tree_deref_slot(slot);
		struct cxlcache_header *chdr;
		struct page *dst_page;

		if (cc_page == NULL)
			continue;

		/* erase cxlcache page */
		spin_lock(&cc_page->cc_page_lock);
		cxlcache_page_delete_from_cc_inode(cc_inode, cc_page);

		/* erase cxlcache header */
		dst_page = virt_to_page(cc_page->handle);
		chdr = (struct cxlcache_header *)dst_page->private;
		cxlcache_header_cache_free(chdr);

		cxlcache_page_free_data(cc_page);
		cxlcache_pool_put(cc_page->pool);
		spin_unlock(&cc_page->cc_page_lock);
		cxlcache_page_cache_free(cc_page);
		atomic_dec(&cxlcache_put_pages);
		cxlcache_update_total_size();
	}

	BUG_ON(cc_inode->cc_page_count != 0);
	cxlcache_inode_erase(&tree->rbroot, cc_inode);
	cxlcache_pool_put(cc_inode->pool);
	spin_unlock(&cc_inode->cc_inode_lock);
	cxlcache_inode_cache_free(cc_inode);
}

/*
 * pool functions
 */

static struct cxlcache_pool *__cxlcache_pool_current(void)
{
	struct cxlcache_pool *pool;

	pool = list_first_or_null_rcu(&cxlcache_pools, typeof(*pool), list);
	WARN_ONCE(!pool && cxlcache_has_pool,
			"%s: no page storage pool!\n", __func__);

	return pool;
}

static struct cxlcache_pool *cxlcache_pool_current(void)
{
	assert_spin_locked(&cxlcache_pools_lock);

	return __cxlcache_pool_current();
}

static struct cxlcache_pool *cxlcache_pool_current_get(void)
{
	struct cxlcache_pool *pool;

	rcu_read_lock();

	pool = __cxlcache_pool_current();
	if (!cxlcache_pool_get(pool))
		pool = NULL;

	rcu_read_unlock();

	return pool;
}

static struct cxlcache_pool *cxlcache_pool_last_get(void)
{
	struct cxlcache_pool *pool, *last = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(pool, &cxlcache_pools, list)
		last = pool;
	WARN_ONCE(!last && cxlcache_has_pool,
			"%s: no page storage pool!\n", __func__);
	if (!cxlcache_pool_get(last))
		last = NULL;

	rcu_read_unlock();

	return last;
}

static void shrink_worker(struct work_struct *w)
{
	struct cxlcache_pool *pool = container_of(w, typeof(*pool), shrink_work);

	if (cxlpool_shrink(pool->cxlpool, 1, NULL, false))
		cxlcache_reject_reclaim_fail++;
	cxlcache_pool_put(pool);
}

static void flush_worker(struct work_struct *w)
{
	struct cxlcache_pool *pool = container_of(w, typeof(*pool), flush_work);

	u64 pages = cxlpool_get_total_size(pool->cxlpool) / PAGE_SIZE;

	if (cxlpool_shrink(pool->cxlpool, pages, NULL, true))
		cxlcache_reject_reclaim_fail++;
	cxlcache_pool_put(pool);

	pages = cxlpool_get_total_size(pool->cxlpool) / PAGE_SIZE;

	if (pages != 0)
		VM_WARN_ON_ONCE(1);

	complete(&pool->done);
}

static struct cxlcache_pool *cxlcache_pool_create(char *type)
{
	struct cxlcache_pool *pool;
	char name[38]; // TODO - zswap using 'zswap' + 32 char (max) num + \0
	gfp_t gfp = __GFP_NORETRY | __GFP_NOWARN | __GFP_KSWAPD_RECLAIM;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	snprintf(name, 38, "cxlcache%x", atomic_inc_return(&cxlcache_pools_count));
	pool->cxlpool = cxlpool_create_pool(type, name, gfp,
			&cxlcache_cxlpool_ops);
	if (!pool->cxlpool) {
		pr_err("%s cxlpool not available\n", type);
		goto error;
	}

	kref_init(&pool->kref);
	INIT_LIST_HEAD(&pool->list);
	INIT_WORK(&pool->shrink_work, shrink_worker);
	INIT_WORK(&pool->flush_work, flush_worker);
	pool->pagesize = PAGE_SIZE;

	return pool;

error:
	kfree(pool);
	return NULL;
}

static __init struct cxlcache_pool *__cxlcache_pool_create_fallback(void)
{
	/* In this situation, this is only simple wrapping function */

	/*
	 * TODO - Does we support a number of pools(e.g. movable or not...)?
	 * If then, we fill this function in the future.
	 * If not, remove this function will be okay.
	 */
	return cxlcache_pool_create(cxlcache_cxlpool_type);
}

static void cxlcache_pool_destroy(struct cxlcache_pool *pool)
{
	pr_debug("cxlcache destroying pool");

	cxlpool_destroy_pool(pool->cxlpool);
	kfree(pool);
}

static int __must_check cxlcache_pool_get(struct cxlcache_pool *pool)
{
	if (!pool)
		return 0;

	return kref_get_unless_zero(&pool->kref);
}

static void __cxlcache_pool_release(struct work_struct *work)
{
	struct cxlcache_pool *pool = container_of(work, typeof(*pool), release_work);

	synchronize_rcu();

	/* nobody should have been able to get a kref... */
	WARN_ON(kref_get_unless_zero(&pool->kref));

	/* pool is now off cxlcache_pools list and has no references. */
	cxlcache_pool_destroy(pool);
}

static void __cxlcache_pool_empty(struct kref *kref)
{
	struct cxlcache_pool *pool;

	pool = container_of(kref, typeof(*pool), kref);

	spin_lock(&cxlcache_pools_lock);

	WARN_ON(pool == cxlcache_pool_current());

	list_del_rcu(&pool->list);

	INIT_WORK(&pool->release_work, __cxlcache_pool_release);
	schedule_work(&pool->release_work);

	spin_unlock(&cxlcache_pools_lock);
}

static void cxlcache_pool_put(struct cxlcache_pool *pool)
{
	kref_put(&pool->kref, __cxlcache_pool_empty);
}

/*
 * param callbacks
 */

static int __cxlcache_param_set(const char *val, const struct kernel_param *kp,
		char *type)
{
	return 0;
}

static int cxlcache_cxlpool_param_set(const char *val,
		const struct kernel_param *kp)
{
	return __cxlcache_param_set(val, kp, NULL);
}

static int cxlcache_enabled_param_set(const char *val,
		const struct kernel_param *kp)
{
	if (cxlcache_init_failed) {
		pr_err("can't enable, initialization failed\n");
		return -ENODEV;
	}
	return param_set_bool(val, kp);
}

static int cxlcache_flush_param_set(const char *val,
		const struct kernel_param *kp)
{
	struct cxlcache_pool *pool;

	if (cxlcache_init_failed) {
		pr_err("can't flush, initialization failed\n");
		return -ENODEV;
	}

	if (!cxlcache_has_pool) {
		pr_err("can't flush, no pool configured\n");
		return -ENODEV;
	}

	if (cxlcache_enabled) {
		pr_err("Flush is supported only when CXL Cache is disabled\n");
		return -EPERM;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(pool, &cxlcache_pools, list)
		if (cxlcache_pool_get(pool)) {
			init_completion(&pool->done);
			queue_work(flush_wq, &pool->flush_work);
			wait_for_completion(&pool->done);
		}
	rcu_read_unlock();
	
	return 0;
}

/*
 * evict function
 */
static int cxlcache_evict(struct cxlpool *pool, unsigned long handle,
		bool under_flush) 
{
	struct cxlcache_tree *tree;
	struct cxlcache_inode *cc_inode;
	struct cxlcache_page *cc_page;
	struct cxlcache_header *chdr;
	struct page *page;
	int pool_id;
	struct cleancache_filekey key = { .u.key = { 0 } };
	int index;
	int ret = 0;
	char *buf;

	buf = cxlpool_map_handle(pool, handle, CXLPOOL_MM_RO);

	page = virt_to_page(buf);

	if (page == NULL) {
		ret = -EINVAL;
		goto end;
	}

	chdr = (struct cxlcache_header*)page->private;
	if (chdr == NULL) {
		ret = -EINVAL;
		goto end;
	}

	pool_id = chdr->pool_id;
	key = chdr->filekey;
	index = chdr->index;

	tree = cxlcache_trees[pool_id];

	cc_inode = cxlcache_inode_search(tree, key);
	if (cc_inode == NULL) {
		cxlcache_header_cache_free(chdr);
		ret = -EINVAL;
		goto end;
	}

	cc_page = cxlcache_index_delete_from_cc_inode(cc_inode, index);
	if (cc_page == NULL) {
		spin_unlock(&cc_inode->cc_inode_lock);
		cxlcache_header_cache_free(chdr);
		ret = -EINVAL;
		goto end;
	}

	/* erase cxlcache inode */
	if (cc_inode->cc_page_count == 0)
		cxlcache_inode_free(tree, cc_inode);
	else
		spin_unlock(&cc_inode->cc_inode_lock);

	cxlcache_evicted_pages++;

	cxlcache_header_cache_free(chdr);

	/* free cxlcache page */
	cxlcache_pool_put(cc_page->pool);
	cxlcache_page_free_data(cc_page);
	spin_unlock(&cc_page->cc_page_lock);
	cxlcache_page_cache_free(cc_page);
	atomic_dec(&cxlcache_put_pages);
	cxlcache_update_total_size();

end:
	cxlpool_unmap_handle(pool, handle);

	return ret;
}

/*
 * cleancache hooks
 */

/* Repalce dupliated page by new page */
static int cxlcache_dup_put_page(struct cxlcache_tree *tree,
		struct cxlcache_page *cc_page,
		struct page *page)
{
	char *buf;
	u8 *src;

	if (!cc_page)
		return -EINVAL;

	BUG_ON(!spin_is_locked(&cc_page->cc_page_lock));

	buf = cxlpool_map_handle(cc_page->pool->cxlpool, cc_page->handle,
			CXLPOOL_MM_WO);

	src = kmap_local_page(page);

	memcpy(buf, src, PAGE_SIZE);

	kunmap_local(src);
	cxlpool_unmap_handle(cc_page->pool->cxlpool, cc_page->handle);

	/* Successfully replaced data, clean up and return success. */
	spin_unlock(&cc_page->cc_page_lock);
	cxlcache_update_total_size();

	return 0;
}

/* attempts to put an single page */
static int cxlcache_put_page(int pool_id, struct cleancache_filekey key,
		pgoff_t index, struct page *page)
{
	struct cxlcache_tree *tree;
	struct cxlcache_inode *cc_inode = NULL;
	struct cxlcache_page *cc_page = NULL;
	struct cxlcache_pool *pool;
	unsigned long handle;
	struct page *dst_page;
	int newinode = 0;
	char *buf;
	u8 *src;
	struct cxlcache_header *chdr;
	gfp_t gfp;
	int ret = 0;

	if (pool_id < 0 || pool_id >= MAX_CXLCACHE_POOL) {
		ret = -EINVAL;
		goto reject;
	}

	tree = cxlcache_trees[pool_id];
	if (tree == NULL) {
		ret = -ENODEV;
		goto reject;
	}

	/* Caching from ZONE_EXMEM isn't supported */
	if (page_zonenum(page) == ZONE_EXMEM) {
		ret = -ENOTSUPP;
		goto reject;
	}

	/* THP isn't supported */
	if (PageTransHuge(page)) {
		ret = -EINVAL;
		goto reject;
	}

	/* CXL cache opt doens't on */
	if (!cxlcache_enabled) {
		ret = -ENODEV;
		goto reject;
	}

refind:
	/* find cxlcache inode at tree */
	if ((cc_inode = cxlcache_inode_search(tree, key)) != NULL) {
		/* find cxlcache page in cxlcache inode. if exist, dup_put function call. */
		if ((cc_page = cxlcache_page_lookup_in_cc_inode(cc_inode, index)) != NULL) {
			if ((ret = cxlcache_dup_put_page(tree, cc_page, page)) != 0)
				goto reject;
			return ret;
		}
	}

	if (cxlcache_is_full()) {
		cxlcache_pool_limit_hit++;
		cxlcache_pool_reached_full = true;
	}

	if (cxlcache_pool_reached_full) {
		if (!cxlcache_can_accept()) {
			if (cc_inode != NULL)
				spin_unlock(&cc_inode->cc_inode_lock);
			ret = -ENOMEM;
			goto shrink;
		} else
			cxlcache_pool_reached_full = false;
	}

	if (cc_inode == NULL) {
		/* allocate cxlcache inode */
		if ((cc_inode = cxlcache_inode_cache_alloc(GFP_KERNEL, key)) == NULL) {
			cxlcache_reject_kmemcache_fail++;
			ret = -ENOMEM;
			goto reject;
		}

		cc_inode->pool = cxlcache_pool_current_get();
		if (!cc_inode->pool) {
			cxlcache_inode_cache_free(cc_inode);
			ret = -EINVAL;
			goto reject;
		}

		/* map rbtree */
		write_lock(&tree->lock);
		if (!cxlcache_inode_insert(&tree->rbroot, cc_inode)) {
			cxlcache_inode_cache_free(cc_inode);
			write_unlock(&tree->lock);
			goto refind;
		}

		spin_lock(&cc_inode->cc_inode_lock);
		newinode = 1;
		write_unlock(&tree->lock);
	}
	/* When arrive here, we have a spinlocked cxlcache inode for use. */
	BUG_ON(!spin_is_locked(&cc_inode->cc_inode_lock));

	/* Allocate cxlcache page */
	if ((cc_page = cxlcache_page_cache_alloc(GFP_KERNEL, cc_inode)) == NULL) {
		/* cxlcache page allocation failed */
		cxlcache_reject_kmemcache_fail++;
		ret = -ENOMEM;
		goto unlock_cc_inode;
	}

	if ((chdr = cxlcache_header_cache_alloc(GFP_KERNEL)) == NULL) {
		/* cxlcache header allocation failed */
		cxlcache_reject_kmemcache_fail++;
		ret = -ENOMEM;
		goto free_cc_page;
	}
	chdr->pool_id = pool_id;
	chdr->filekey = key;
	chdr->index = index;

	cc_page->pool = cxlcache_pool_current_get();
	if (!cc_page->pool) {
		ret = -EINVAL;
		goto free_chdr;
	}

	/* Allocate 4KB page from cxlpool */
	gfp = __GFP_NORETRY | __GFP_NOWARN | __GFP_KSWAPD_RECLAIM | __GFP_EXMEM;
	if (cxlpool_malloc(cc_page->pool->cxlpool, PAGE_SIZE, gfp, &handle) < 0) {
		cxlcache_reject_alloc_fail++;
		ret = -ENOMEM;
		goto put_dstmem;
	}

	spin_lock(&cc_page->cc_page_lock);

	/* map cxlcache page at cxlcache inode's radix tree */
	if ((ret = cxlcache_page_add_to_cc_inode(cc_inode, index, cc_page)) < 0) {
		spin_unlock(&cc_page->cc_page_lock);
		goto put_dstmem;
	}

	spin_unlock(&cc_inode->cc_inode_lock);

	buf = cxlpool_map_handle(cc_page->pool->cxlpool, handle,
			CXLPOOL_MM_WO);

	dst_page = virt_to_page(handle);
	dst_page->private = (unsigned long)chdr;

	src = kmap_local_page(page);

	memcpy(buf, src, PAGE_SIZE);

	kunmap_local(src);
	cxlpool_unmap_handle(cc_page->pool->cxlpool, handle);

	/* populate cxlcache page descriptor */
	cc_page->index = index;
	cc_page->handle = handle;
	cc_page->size = PAGE_SIZE;


	spin_unlock(&cc_page->cc_page_lock);

	atomic_inc(&cxlcache_put_pages);
	cxlcache_update_total_size();

	return ret;

put_dstmem:
	cxlcache_pool_put(cc_page->pool);
free_chdr:
	cxlcache_header_cache_free(chdr);
free_cc_page:
	cxlcache_page_cache_free(cc_page);
unlock_cc_inode:
	if (newinode)
		cxlcache_inode_free(tree, cc_inode);
	else
		spin_unlock(&cc_inode->cc_inode_lock);

reject:
	cxlcache_invalidate_page(pool_id, key, index);
	return ret;

shrink:
	pool = cxlcache_pool_last_get();
	if (pool)
		queue_work(shrink_wq, &pool->shrink_work);
	goto reject;
}

static int cxlcache_get_page(int pool_id, struct cleancache_filekey key,
		pgoff_t index, struct page *page)
{
	struct cxlcache_tree *tree;
	struct cxlcache_inode *cc_inode;
	struct cxlcache_page *cc_page;
	u8 *src, *dst;

	if (pool_id < 0 || pool_id >= MAX_CXLCACHE_POOL)
		return -EINVAL;

	tree = cxlcache_trees[pool_id];
	if (tree == NULL)
		return -EINVAL;

	/* find cleancache inode by filekey */
	cc_inode = cxlcache_inode_search(tree, key);
	if (cc_inode == NULL)
		return -ENOENT;

	BUG_ON(!spin_is_locked(&cc_inode->cc_inode_lock));

	/* find cxlcache page using page index */
	cc_page = cxlcache_page_lookup_in_cc_inode(cc_inode, index);
	if (cc_page == NULL) {
		spin_unlock(&cc_inode->cc_inode_lock);
		return -ENOENT;
	}

	/* data copy from cxlcache page */
	src = cxlpool_map_handle(cc_page->pool->cxlpool, cc_page->handle,
			CXLPOOL_MM_RO);
	dst = kmap_local_page(page);

	memcpy(dst, src, cc_page->size);

	kunmap_local(dst);
	cxlpool_unmap_handle(cc_page->pool->cxlpool, cc_page->handle);

	spin_unlock(&cc_page->cc_page_lock);

	return 0;
}


static void cxlcache_invalidate_page(int pool_id, struct cleancache_filekey key,
		pgoff_t index)
{
	struct cxlcache_tree *tree;
	struct cxlcache_inode *cc_inode;
	struct cxlcache_page *cc_page;
	struct page *dst_page;
	struct cxlcache_header *chdr;

	if (pool_id < 0 || pool_id >= MAX_CXLCACHE_POOL)
		return;

	tree = cxlcache_trees[pool_id];
	if (tree == NULL)
		return;

	cc_inode = cxlcache_inode_search(tree, key);
	if (cc_inode == NULL)
		return;

	if ((cc_page = cxlcache_index_delete_from_cc_inode(cc_inode, index)) == NULL) {
		spin_unlock(&cc_inode->cc_inode_lock);
		return;
	}

	/* erase cxlcache inode */
	if (cc_inode->cc_page_count == 0)
		cxlcache_inode_free(tree, cc_inode);
	else
		spin_unlock(&cc_inode->cc_inode_lock);


	/* erase cxlcache header */
	dst_page = virt_to_page(cc_page->handle);
	chdr = (struct cxlcache_header *)dst_page->private;
	cxlcache_header_cache_free(chdr);

	/* erase cxlcache page */
	cxlcache_page_free_data(cc_page);
	cxlcache_pool_put(cc_page->pool);
	spin_unlock(&cc_page->cc_page_lock);
	cxlcache_page_cache_free(cc_page);

	atomic_dec(&cxlcache_put_pages);
	cxlcache_update_total_size();
}

static void cxlcache_invalidate_inode(int pool_id, struct cleancache_filekey key)
{
	struct cxlcache_tree *tree;
	struct cxlcache_inode *cc_inode;

	if (pool_id < 0 || pool_id >= MAX_CXLCACHE_POOL)
		return;

	tree = cxlcache_trees[pool_id];
	if (tree == NULL)
		return;

	cc_inode = cxlcache_inode_search(tree, key);
	if (cc_inode == NULL)
		return;

	write_lock(&tree->lock);
	cxlcache_inode_destroy(tree, cc_inode);
	write_unlock(&tree->lock);
}

static void cxlcache_invalidate_fs(int pool_id)
{
	struct cxlcache_tree *tree;
	struct cxlcache_inode *cc_inode;
	struct rb_node *node;

	if (pool_id < 0 || pool_id >= MAX_CXLCACHE_POOL)
		return;

	tree = cxlcache_trees[pool_id];
	if (tree == NULL)
		return;

	write_lock(&tree->lock);

	node = rb_first(&tree->rbroot);
	while (node != NULL) {
		cc_inode = container_of(node, struct cxlcache_inode, rb_tree_node);
		spin_lock(&cc_inode->cc_inode_lock);
		node = rb_next(node);
		cxlcache_inode_destroy(tree, cc_inode);
	}

	tree->rbroot = RB_ROOT;
	write_unlock(&tree->lock);
	kfree(tree);

	spin_lock(&cxlcache_tree_lock);
	cxlcache_trees[pool_id] = NULL;
	spin_unlock(&cxlcache_tree_lock);
}

static int cxlcache_init_fs(size_t pagesize)
{
	struct cxlcache_tree *tree;
	int pool_id;

	spin_lock(&cxlcache_tree_lock);
	for (pool_id = 0; pool_id < MAX_CXLCACHE_POOL; pool_id++)
		if (cxlcache_trees[pool_id] == NULL)
			break;
	spin_unlock(&cxlcache_tree_lock);

	if (pool_id >= MAX_CXLCACHE_POOL)
		goto fail;

	tree = kzalloc(sizeof(*tree), GFP_KERNEL);
	if (!tree)
		goto fail;

	tree->rbroot = RB_ROOT;
	rwlock_init(&tree->lock);
	cxlcache_trees[pool_id] = tree;

	return pool_id;
fail:
	pr_err("cxlcache tree alloc failed, disabled cxlcache\n");
	return -2;
};

static int cxlcache_init_shared_fs(uuid_t *uuid, size_t pagesize)
{
	/*TODO: Currently, cxlcache doesn't support shared pool.*/
	return -2;
}

static struct cleancache_ops cxlcache_ops = {
	.init_fs = cxlcache_init_fs,
	.init_shared_fs = cxlcache_init_shared_fs,
	.get_page = cxlcache_get_page,
	.put_page = cxlcache_put_page,
	.invalidate_page = cxlcache_invalidate_page,
	.invalidate_inode = cxlcache_invalidate_inode,
	.invalidate_fs = cxlcache_invalidate_fs
};

/*
 * debugfs functions
 */
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *cxlcache_debugfs_root;

static int __init cxlcache_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	cxlcache_debugfs_root = debugfs_create_dir("cxlcache", NULL);

	debugfs_create_u64("pool_limit_hit", 0444,
				cxlcache_debugfs_root, &cxlcache_pool_limit_hit);
	debugfs_create_u64("reject_reclaim_fail", 0444,
				cxlcache_debugfs_root, &cxlcache_reject_reclaim_fail);
	debugfs_create_u64("reject_alloc_fail", 0444,
				cxlcache_debugfs_root, &cxlcache_reject_alloc_fail);
	debugfs_create_u64("reject_kmemcache_fail", 0444,
				cxlcache_debugfs_root, &cxlcache_reject_kmemcache_fail);
	debugfs_create_u64("evicted_pages", 0444,
				cxlcache_debugfs_root, &cxlcache_evicted_pages);
	debugfs_create_u64("pool_total_size", 0444,
				cxlcache_debugfs_root, &cxlcache_pool_total_size);
	debugfs_create_atomic_t("put_pages", 0444,
				cxlcache_debugfs_root, &cxlcache_put_pages);

	return 0;
}
#else
static int __init cxlcache_debugfs_init(void)
{
	return 0;
}
#endif

/*
 * module init and exit
 */
static int __init init_cxlcache(void)
{
	struct cxlcache_pool *pool;
	int ret;

	if (cxlcache_inode_cache_create()) {
		pr_err("cxlcache ot cache creation failed\n");
		goto inode_cache_fail;
	}

	if (cxlcache_page_cache_create()) {
		pr_err("cxlcache page descriptor cache creation failed\n");
		goto page_cache_fail;
	}

	if (cxlcache_header_cache_create()) {
		pr_err("cxlcache header cache creation failed\n");
		goto header_cache_fail;
	}

	pool = __cxlcache_pool_create_fallback();
	if (pool) {
		pr_info("cxlcache using pool (allocator: %s)\n",
				cxlpool_get_type(pool->cxlpool));
		list_add(&pool->list, &cxlcache_pools);
		cxlcache_has_pool = true;
	} else {
		pr_err("cxlcache pool creation failed\n");
		cxlcache_has_pool = false;
		goto pool_fail;
	}

	shrink_wq = create_workqueue("cxlcache-shrink");
	if (!shrink_wq)
		goto create_wq_fail;

	flush_wq = create_workqueue("cxlcache-flush");
	if (!flush_wq)
		goto destroy_shrink_wq;

	/* Connect cleancache_ops */
	ret = cleancache_register_ops(&cxlcache_ops);
	if (ret)
		goto destroy_wq;

	spin_lock_init(&cxlcache_tree_lock);

	if (cxlcache_debugfs_init())
		pr_warn("debugfs_initialization failed\n");

	return 0;

destroy_wq:
	destroy_workqueue(flush_wq);
destroy_shrink_wq:
	destroy_workqueue(shrink_wq);
create_wq_fail:
	cxlcache_pool_destroy(pool);
pool_fail:
	cxlcache_header_cache_destroy();
header_cache_fail:
	cxlcache_page_cache_destroy();
page_cache_fail:
	cxlcache_inode_cache_destroy();
inode_cache_fail:
	cxlcache_init_failed = true;
	cxlcache_enabled = false;
	return -ENOMEM;
}

/* TODO
 * CXL cache is consisted with FS. We need change to fs_initcall?
 */
late_initcall(init_cxlcache)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hojin Nam <hj96.nam@samsung.com>");
MODULE_DESCRIPTION("Clean file Cache for page cache using CXL Devices");
