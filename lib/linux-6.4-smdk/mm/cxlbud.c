#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/cxlpool.h>
#include <linux/page-flags.h>

struct cxlbud_pool;

struct cxlbud_ops {
	int (*evict)(struct cxlbud_pool *pool, unsigned long handle, 
				bool under_flush);
};

struct cxlbud_pool {
	spinlock_t lock;
	struct list_head lru;
	atomic64_t pages_nr;
	const struct cxlbud_ops *ops;
	struct cxlpool *cxlpool;
	const struct cxlpool_ops *cxlpool_ops;
};

/* Dummy struct */
struct cxlbud_header {
};

/* Initializes the cxlbud header of a newly allocated cxlbud page */
static struct cxlbud_header *init_cxlbud_page(struct page *page)
{
	struct cxlbud_header *chdr = page_address(page);
	ClearPageReclaim(page);
	INIT_LIST_HEAD(&page->lru);
	return chdr;
}

/* Resets the struct page fields and frees the page */
static void free_cxlbud_page(struct page *page)
{
	__free_page(page);
}

/* Encodes the handle of a cxlbud page */
static unsigned long encode_handle(struct cxlbud_header *chdr)
{
	return (unsigned long)chdr;
}

/* Returns the cxlbud page where a given handle is stored */
static inline struct cxlbud_header *handle_to_cxlbud_header(unsigned long handle)
{
	return (struct cxlbud_header *)(handle & PAGE_MASK);
}

/*
 * API Functions
 */
/**
 * cxlbud_create_pool() - create a new cxlbud pool
 * @gfp:	gfp flags when allocating the cxlbud pool structure
 * @ops:	user-defined operations for the cxlbud pool
 *
 * Return: pointer to the new cxlbud pool or NULL if the metadata allocation
 * failed.
 */
static struct cxlbud_pool *cxlbud_create_pool(gfp_t gfp,
				const struct cxlbud_ops *ops)
{
	struct cxlbud_pool *pool;

	pool = kzalloc(sizeof(struct cxlbud_pool), gfp);
	if (!pool)
		return NULL;
	spin_lock_init(&pool->lock);
	INIT_LIST_HEAD(&pool->lru);
	atomic64_set(&pool->pages_nr, 0);
	pool->ops = ops;
	return pool;
}

/**
 * cxlbud_destroy_pool() - destroys an existing cxlbud pool
 * @pool:	the cxlbud pool to be destroyed
 *
 * The pool should be emptied before this function is called.
 */
static void cxlbud_destroy_pool(struct cxlbud_pool *pool)
{
	kfree(pool);
}

/**
 * cxlbud_alloc() - allocates a region of a given size
 * @pool:	cxlbud pool from which to allocate
 * @size:	size in bytes of the desired allocation
 * @gfp:	gfp flags used if the pool needs to grow
 * @handle:	handle of the new allocation
 *
 * This function will allocate a new page and add to the pool to satisfy the
 * request.
 *
 * gfp should not set __GFP_HIGHMEM or __GFP_MOVABLE.
 *
 * Return: 0 if success and handle is set, otherwise -EINVAL if the size or
 * gfp arguments are invalid or -ENOMEM if the pool was unable to allocate
 * a new page.
 */
static int cxlbud_alloc(struct cxlbud_pool *pool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	struct page *page;
	struct cxlbud_header *chdr = NULL;

	if ((gfp & (__GFP_HIGHMEM | __GFP_MOVABLE)) || !(gfp & __GFP_EXMEM))
		return -EINVAL;

	if (!size)
		return -EINVAL;

	// TODO - Per CPU CXL Swap Page Cache.

	page = __alloc_pages(gfp, 0, numa_node_id(), NULL);
	if (!page)
		return -ENOMEM;

	spin_lock(&pool->lock);

	chdr = init_cxlbud_page(page);
	atomic64_inc(&pool->pages_nr);

	list_add(&page->lru, &pool->lru);

	*handle = encode_handle(chdr);

	spin_unlock(&pool->lock);

	return 0;
}

/**
 * cxlbud_free() - frees the allocation associated with the given handle
 * @pool:	pool in which the allocation resided
 * @handle:	handle associated with the allocation returned by cxlbud_alloc()
 *
 * In the case that the cxlbud page in which the allocation resides is under
 * reclaim, as indicated by the PG_reclaim flag being set, this function
 * only returns. The page is actually freed once it is evicted (see
 * cxlbud_reclaim_page() below).
 */
static void cxlbud_free(struct cxlbud_pool *pool, unsigned long handle)
{
	struct cxlbud_header *chdr;
	struct page *page;

	spin_lock(&pool->lock);

	chdr = handle_to_cxlbud_header(handle);
	page = virt_to_page(chdr);

	if (PageReclaim(page)) {
		/* cxlbud page is under reclaim, reclaim will free */
		spin_unlock(&pool->lock);
		return;
	}

	list_del_init(&page->lru);
	free_cxlbud_page(page);
	atomic64_dec(&pool->pages_nr);

	spin_unlock(&pool->lock);
}

/**
 * cxlbud_reclaim_page() - evicts allocations from a pool page and frees it
 * @pool:	pool from which a page will attempt to be evicted
 * @retries:	number of pages on the LRU list for which eviction will
 *		be attempted before failing
 *
 * Returns: 0 if page is successfully freed, otherwise -EINVAL if there are
 * no pages to evict or an eviction handler is not registered, -EAGAIN if
 * the retry limit was hit.
 */
static int cxlbud_reclaim_page(struct cxlbud_pool *pool, unsigned int retries,
				bool under_flush)
{
	int i, ret = -1;
	struct cxlbud_header *chdr = NULL;
	struct page *page = NULL;
	unsigned long handle = 0;

	spin_lock(&pool->lock);
	if (!pool->ops || !pool->ops->evict || list_empty(&pool->lru) ||
			retries == 0) {
		spin_unlock(&pool->lock);
		return -EINVAL;
	}
	for (i = 0; i < retries; i++) {
		if (list_empty(&pool->lru))
			break;

		page = list_last_entry(&pool->lru, struct page, lru);
		chdr = page_address(page);
		if (!chdr)
			break;

		list_del_init(&page->lru);
		/* Protect cxlbud page against free */
		SetPageReclaim(page);
		spin_unlock(&pool->lock);

		handle = encode_handle(chdr);

		ret = pool->ops->evict(pool, handle, under_flush);

		spin_lock(&pool->lock);
		ClearPageReclaim(page);
		if (!ret) {
			/* free the cxlbud page and return success. */
			free_cxlbud_page(page);
			atomic64_dec(&pool->pages_nr);
			spin_unlock(&pool->lock);
			return 0;
		}

		/* add to beginning of LRU */
		list_add(&page->lru, &pool->lru);
	}
	spin_unlock(&pool->lock);
	return -EAGAIN;
}

/**
 * cxlbud_map() - maps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be mapped
 *
 * Returns: a pointer to the mapped allocation
 */
static void *cxlbud_map(struct cxlbud_pool *pool, unsigned long handle)
{
	return (void *)(handle);
}

/**
 * cxlbud_unmap() - maps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be unmapped
 */
static void cxlbud_unmap(struct cxlbud_pool *pool, unsigned long handle)
{
}

/**
 * cxlbud_get_pool_size() - gets the cxlbud pool size in pages
 * @pool:	pool whose size is being queried
 *
 * Returns: size in pages of the given pool.  The pool lock need not be
 * taken to access pages_nr.
 */
static u64 cxlbud_get_pool_size(struct cxlbud_pool *pool)
{
	return atomic64_read(&pool->pages_nr);
}

/*
 * cxlpool
 */

static int cxlbud_cxlpool_evict(struct cxlbud_pool *pool, unsigned long handle,
				bool under_flush)
{
	if (pool->cxlpool && pool->cxlpool_ops && pool->cxlpool_ops->evict)
		return pool->cxlpool_ops->evict(pool->cxlpool, handle, under_flush);
	else
		return -ENOENT;
}

static const struct cxlbud_ops cxlbud_cxlpool_ops = {
	.evict =	cxlbud_cxlpool_evict
};

static void *cxlbud_cxlpool_create(const char *name, gfp_t gfp,
				const struct cxlpool_ops *cxlpool_ops,
				struct cxlpool *cxlpool)
{
	struct cxlbud_pool *pool;

	pool = cxlbud_create_pool(gfp, cxlpool_ops ? &cxlbud_cxlpool_ops : NULL);
	if (pool) {
		pool->cxlpool = cxlpool;
		pool->cxlpool_ops = cxlpool_ops;
	}
	return pool;
}

static void cxlbud_cxlpool_destroy(void *pool)
{
	cxlbud_destroy_pool(pool);
}

static int cxlbud_cxlpool_malloc(void *pool, size_t size, gfp_t gfp,
				unsigned long *handle)
{
	return cxlbud_alloc(pool, size, gfp, handle);
}

static void cxlbud_cxlpool_free(void *pool, unsigned long handle)
{
	cxlbud_free(pool, handle);
}

static int cxlbud_cxlpool_shrink(void *pool, unsigned int pages,
			unsigned int *reclaimed, bool under_flush)
{
	unsigned int total = 0;
	int ret = -EINVAL;

	while (total < pages) {
		ret = cxlbud_reclaim_page(pool, 8, under_flush);
		if (ret < 0)
			break;
		total++;
	}

	if (reclaimed)
		*reclaimed = total;

	return ret;
}

static void *cxlbud_cxlpool_map(void *pool, unsigned long handle,
				enum cxlpool_mapmode mm)
{
	return cxlbud_map(pool, handle);
}

static void cxlbud_cxlpool_unmap(void *pool, unsigned long handle)
{
	cxlbud_unmap(pool, handle);
}

static u64 cxlbud_cxlpool_total_size(void *pool)
{
	return cxlbud_get_pool_size(pool) * PAGE_SIZE;
}

static struct cxlpool_driver cxlbud_cxlpool_driver = {
	.type =     "cxlbud",
	.sleep_mapped = true,
	.owner =    THIS_MODULE,
	.create =   cxlbud_cxlpool_create,
	.destroy =  cxlbud_cxlpool_destroy,
	.malloc =   cxlbud_cxlpool_malloc,
	.free =     cxlbud_cxlpool_free,
	.shrink =   cxlbud_cxlpool_shrink,
	.map =      cxlbud_cxlpool_map,
	.unmap =    cxlbud_cxlpool_unmap,
	.total_size =   cxlbud_cxlpool_total_size,
};

MODULE_ALIAS("cxlpool-cxlbud");

static int __init init_cxlbud(void)
{
	pr_info("loaded\n");

	cxlpool_register_driver(&cxlbud_cxlpool_driver);

	return 0;
}

static void __exit exit_cxlbud(void)
{
	cxlpool_unregister_driver(&cxlbud_cxlpool_driver);
	pr_info("unloaded\n");
}

module_init(init_cxlbud);
module_exit(exit_cxlbud);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Seungjun Ha <sseungjun.ha@samsung.com>");
MODULE_DESCRIPTION("Buddy Allocator for CXL Swap (Non-Compressed)");
