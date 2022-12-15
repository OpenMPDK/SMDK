#include <linux/list.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/cxlpool.h>

struct cxlpool {
	struct cxlpool_driver *driver;
	void *pool;
	const struct cxlpool_ops *ops;
	bool evictable;
	bool can_sleep_mapped;
};

static LIST_HEAD(drivers_head);
static DEFINE_SPINLOCK(drivers_lock);

void cxlpool_register_driver(struct cxlpool_driver *driver)
{
	spin_lock(&drivers_lock);
	atomic_set(&driver->refcount, 0);
	list_add(&driver->list, &drivers_head);
	spin_unlock(&drivers_lock);
}
EXPORT_SYMBOL(cxlpool_register_driver);

int cxlpool_unregister_driver(struct cxlpool_driver *driver)
{
	int ret = 0, refcount;

	spin_lock(&drivers_lock);
	refcount = atomic_read(&driver->refcount);
	WARN_ON(refcount < 0);
	if(refcount > 0)
		ret = -EBUSY;
	else
		list_del(&driver->list);
	spin_unlock(&drivers_lock);

	return ret;
}
EXPORT_SYMBOL(cxlpool_unregister_driver);

static struct cxlpool_driver *cxlpool_get_driver(const char *type)
{
	struct cxlpool_driver *driver;

	spin_lock(&drivers_lock);
	list_for_each_entry(driver, &drivers_head, list) {
		if (!strcmp(driver->type, type)) {
			bool got = try_module_get(driver->owner);

			if (got)
				atomic_inc(&driver->refcount);
			spin_unlock(&drivers_lock);
			return got ? driver : NULL;
		}
	}

	spin_unlock(&drivers_lock);
	return NULL;
}

static void cxlpool_put_driver(struct cxlpool_driver *driver)
{
	atomic_dec(&driver->refcount);
	module_put(driver->owner);
}

bool cxlpool_has_pool(char *type)
{
	struct cxlpool_driver *driver = cxlpool_get_driver(type);

	if (!driver) {
		request_module("cxlpool-%s", type);
		driver = cxlpool_get_driver(type);
	}

	if (!driver)
		return false;

	cxlpool_put_driver(driver);
	return true;
}
EXPORT_SYMBOL(cxlpool_has_pool);

struct cxlpool *cxlpool_create_pool(const char *type, const char *name,
		gfp_t gfp, const struct cxlpool_ops *ops)
{
	struct cxlpool_driver *driver;
	struct cxlpool *cxlpool;

	driver = cxlpool_get_driver(type);
	if (!driver) {
		request_module("cxlpool-%s", type);
		driver = cxlpool_get_driver(type);
	}

	if (!driver) {
		pr_err("no driver for type %s\n", type);
		return NULL;
	}

	cxlpool = kmalloc(sizeof(*cxlpool), gfp);
	if (!cxlpool) {
		pr_err("couldn't create cxlpool - out of memory\n");
		cxlpool_put_driver(driver);
		return NULL;
	}

	cxlpool->driver = driver;
	cxlpool->pool = driver->create(name, gfp, ops, cxlpool);
	cxlpool->ops = ops;
	cxlpool->evictable = driver->shrink && ops && ops->evict;
	cxlpool->can_sleep_mapped = false;

	if (!cxlpool->pool) {
		pr_err("couldn't create %s pool\n", type);
		cxlpool_put_driver(driver);
		kfree(cxlpool);
		return NULL;
	}

	return cxlpool;
}

void cxlpool_destroy_pool(struct cxlpool *cxlpool)
{
	pr_debug("destroying cxlpool type %s\n", cxlpool->driver->type);

	cxlpool->driver->destroy(cxlpool->pool);
	cxlpool_put_driver(cxlpool->driver);
	kfree(cxlpool);
}

const char *cxlpool_get_type(struct cxlpool *cxlpool)
{
	return cxlpool->driver->type;
}

int cxlpool_malloc(struct cxlpool *cxlpool, size_t size, gfp_t gfp,
		unsigned long *handle)
{
	return cxlpool->driver->malloc(cxlpool->pool, size, gfp, handle);
}

void cxlpool_free(struct cxlpool *cxlpool, unsigned long handle)
{
	cxlpool->driver->free(cxlpool->pool, handle);
}

int cxlpool_shrink(struct cxlpool *cxlpool, unsigned int pages,
		unsigned int *reclaimed, bool under_flush)
{
	return cxlpool->driver->shrink ?
		cxlpool->driver->shrink(cxlpool->pool, pages, reclaimed,
				under_flush) : -EINVAL;
}

void *cxlpool_map_handle(struct cxlpool *cxlpool, unsigned long handle,
		enum cxlpool_mapmode mapmode)
{
	return cxlpool->driver->map(cxlpool->pool, handle, mapmode);
}

void cxlpool_unmap_handle(struct cxlpool *cxlpool, unsigned long handle)
{
	cxlpool->driver->unmap(cxlpool->pool, handle);
}

u64 cxlpool_get_total_size(struct cxlpool *cxlpool)
{
	return cxlpool->driver->total_size(cxlpool->pool);
}

bool cxlpool_evictable(struct cxlpool *cxlpool)
{
	return cxlpool->evictable;
}

bool cxlpool_can_sleep_mapped(struct cxlpool *cxlpool)
{
	return cxlpool->can_sleep_mapped;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Seungjun Ha <seungjun.ha@samsung.com>");
MODULE_DESCRIPTION("Pool for CXL Swap");
