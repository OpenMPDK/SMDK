#ifndef _CXLPOOL_H_
#define _CXLPOOL_H_

struct cxlpool;

struct cxlpool_ops {
    int (*evict)(struct cxlpool *pool, unsigned long handle, bool under_flush);
};

enum cxlpool_mapmode {
    CXLPOOL_MM_RW, /* normal read-write mapping */
    CXLPOOL_MM_RO, /* read-only (no copy-out at unmap time) */
    CXLPOOL_MM_WO, /* write-only (no copy-in at map time) */

    CXLPOOL_MM_DEFAULT = CXLPOOL_MM_RW
};

bool cxlpool_has_pool(char *type);

struct cxlpool *cxlpool_create_pool(const char *type, const char *name,
			gfp_t gfp, const struct cxlpool_ops *ops);

const char *cxlpool_get_type(struct cxlpool *pool);

void cxlpool_destroy_pool(struct cxlpool *pool);

bool cxlpool_malloc_support_movable(struct cxlpool *pool);

int cxlpool_malloc(struct cxlpool *pool, size_t size, gfp_t gfp,
			unsigned long *handle);

void cxlpool_free(struct cxlpool *pool, unsigned long handle);

int cxlpool_shrink(struct cxlpool *pool, unsigned int pages,
			unsigned int *reclaimed, bool under_flush);

void *cxlpool_map_handle(struct cxlpool *pool, unsigned long handle,
			enum cxlpool_mapmode mm);

void cxlpool_unmap_handle(struct cxlpool *pool, unsigned long handle);

u64 cxlpool_get_total_size(struct cxlpool *pool);

struct cxlpool_driver {
	char *type;
	struct module *owner;
	atomic_t refcount;
	struct list_head list;

	void *(*create)(const char *name,
			gfp_t gfp,
			const struct cxlpool_ops *ops,
			struct cxlpool *cxlpool);
	void (*destroy)(void *pool);

	bool malloc_support_movable;
	int (*malloc)(void *pool, size_t size, gfp_t gfp,
				unsigned long *handle);
	void (*free)(void *pool, unsigned long handle);

	int (*shrink)(void *pool, unsigned int pages,
				unsigned int *reclaimed, bool under_flush);

	bool sleep_mapped;
	void *(*map)(void *pool, unsigned long handle,
				enum cxlpool_mapmode mm);
	void (*unmap)(void *pool, unsigned long handle);

	u64 (*total_size)(void *pool);
};

void cxlpool_register_driver(struct cxlpool_driver *driver);

int cxlpool_unregister_driver(struct cxlpool_driver *driver);

bool cxlpool_evictable(struct cxlpool *pool);
bool cxlpool_can_sleep_mapped(struct cxlpool *pool);

#endif
