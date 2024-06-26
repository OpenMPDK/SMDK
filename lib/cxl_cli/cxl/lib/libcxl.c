// SPDX-License-Identifier: LGPL-2.1
// Copyright (C) 2020-2021, Intel Corporation. All rights reserved.
#include <poll.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <uuid/uuid.h>
#include <ccan/list/list.h>
#include <ccan/endian/endian.h>
#include <ccan/minmax/minmax.h>
#include <ccan/array_size/array_size.h>
#include <ccan/short_types/short_types.h>
#include <ccan/container_of/container_of.h>

#include <util/log.h>
#include <util/list.h>
#include <util/size.h>
#include <util/sysfs.h>
#include <util/bitmap.h>
#include <cxl/cxl_mem.h>
#include <cxl/libcxl.h>
#include <daxctl/libdaxctl.h>
#include "private.h"

/**
 * struct cxl_ctx - library user context to find "nd" instances
 *
 * Instantiate with cxl_new(), which takes an initial reference.  Free
 * the context by dropping the reference count to zero with
 * cxl_unref(), or take additional references with cxl_ref()
 * @timeout: default library timeout in milliseconds
 */
struct cxl_ctx {
	/* log_ctx must be first member for cxl_set_log_fn compat */
	struct log_ctx ctx;
	int refcount;
	void *userdata;
	int memdevs_init;
	int buses_init;
	unsigned long timeout;
	struct udev *udev;
	struct udev_queue *udev_queue;
	struct list_head memdevs;
	struct list_head buses;
	struct kmod_ctx *kmod_ctx;
	struct daxctl_ctx *daxctl_ctx;
	void *private_data;
};

static void free_pmem(struct cxl_pmem *pmem)
{
	if (pmem) {
		free(pmem->dev_buf);
		free(pmem->dev_path);
		free(pmem);
	}
}

static void free_fwl(struct cxl_fw_loader *fwl)
{
	if (fwl) {
		free(fwl->loading);
		free(fwl->data);
		free(fwl->remaining);
		free(fwl->cancel);
		free(fwl->status);
		free(fwl);
	}
}

static void free_memdev(struct cxl_memdev *memdev, struct list_head *head)
{
	if (head)
		list_del_from(head, &memdev->list);
	kmod_module_unref(memdev->module);
	free_pmem(memdev->pmem);
	free_fwl(memdev->fwl);
	free(memdev->firmware_version);
	free(memdev->dev_buf);
	free(memdev->dev_path);
	free(memdev->host_path);
	free(memdev);
}

static void free_target(struct cxl_target *target, struct list_head *head)
{
	if (head)
		list_del_from(head, &target->list);
	free(target->dev_path);
	free(target->phys_path);
	free(target->fw_path);
	free(target);
}

static void free_region(struct cxl_region *region, struct list_head *head)
{
	struct cxl_memdev_mapping *mapping, *_m;

	list_for_each_safe(&region->mappings, mapping, _m, list) {
		list_del_from(&region->mappings, &mapping->list);
		free(mapping);
	}
	if (head)
		list_del_from(head, &region->list);
	kmod_module_unref(region->module);
	free(region->dev_buf);
	free(region->dev_path);
	free(region);
}

static void free_stale_regions(struct cxl_decoder *decoder)
{
	struct cxl_region *region, *_r;

	list_for_each_safe(&decoder->stale_regions, region, _r, list)
		free_region(region, &decoder->stale_regions);
}

static void free_regions(struct cxl_decoder *decoder)
{
	struct cxl_region *region, *_r;

	list_for_each_safe(&decoder->regions, region, _r, list)
		free_region(region, &decoder->regions);
}

static void free_decoder(struct cxl_decoder *decoder, struct list_head *head)
{
	struct cxl_target *target, *_t;

	if (head)
		list_del_from(head, &decoder->list);
	list_for_each_safe(&decoder->targets, target, _t, list)
		free_target(target, &decoder->targets);
	free_regions(decoder);
	free_stale_regions(decoder);
	free(decoder->dev_buf);
	free(decoder->dev_path);
	free(decoder);
}

static void free_dport(struct cxl_dport *dport, struct list_head *head)
{
	if (head)
		list_del_from(head, &dport->list);
	free(dport->dev_buf);
	free(dport->dev_path);
	free(dport->phys_path);
	free(dport->fw_path);
	free(dport);
}

static void free_port(struct cxl_port *port, struct list_head *head);
static void free_endpoint(struct cxl_endpoint *endpoint, struct list_head *head);
static void __free_port(struct cxl_port *port, struct list_head *head)
{
	struct cxl_endpoint *endpoint, *_e;
	struct cxl_decoder *decoder, *_d;
	struct cxl_dport *dport, *_dp;
	struct cxl_port *child, *_c;

	if (head)
		list_del_from(head, &port->list);
	list_for_each_safe(&port->child_ports, child, _c, list)
		free_port(child, &port->child_ports);
	list_for_each_safe(&port->endpoints, endpoint, _e, port.list)
		free_endpoint(endpoint, &port->endpoints);
	list_for_each_safe(&port->decoders, decoder, _d, list)
		free_decoder(decoder, &port->decoders);
	list_for_each_safe(&port->dports, dport, _dp, list)
		free_dport(dport , &port->dports);
	kmod_module_unref(port->module);
	free(port->dev_buf);
	free(port->dev_path);
	free(port->uport);
	free(port->parent_dport_path);
}

static void free_port(struct cxl_port *port, struct list_head *head)
{
	__free_port(port, head);
	free(port);
}

static void free_endpoint(struct cxl_endpoint *endpoint, struct list_head *head)
{
	__free_port(&endpoint->port, head);
	free(endpoint);
}

static void free_bus(struct cxl_bus *bus, struct list_head *head)
{
	__free_port(&bus->port, head);
	free(bus);
}

/**
 * cxl_get_userdata - retrieve stored data pointer from library context
 * @ctx: cxl library context
 *
 * This might be useful to access from callbacks like a custom logging
 * function.
 */
CXL_EXPORT void *cxl_get_userdata(struct cxl_ctx *ctx)
{
	if (ctx == NULL)
		return NULL;
	return ctx->userdata;
}

/**
 * cxl_set_userdata - store custom @userdata in the library context
 * @ctx: cxl library context
 * @userdata: data pointer
 */
CXL_EXPORT void cxl_set_userdata(struct cxl_ctx *ctx, void *userdata)
{
	if (ctx == NULL)
		return;
	ctx->userdata = userdata;
}

CXL_EXPORT void cxl_set_private_data(struct cxl_ctx *ctx, void *data)
{
	ctx->private_data = data;
}

CXL_EXPORT void *cxl_get_private_data(struct cxl_ctx *ctx)
{
	return ctx->private_data;
}

/**
 * cxl_new - instantiate a new library context
 * @ctx: context to establish
 *
 * Returns zero on success and stores an opaque pointer in ctx.  The
 * context is freed by cxl_unref(), i.e. cxl_new() implies an
 * internal cxl_ref().
 */
CXL_EXPORT int cxl_new(struct cxl_ctx **ctx)
{
	struct daxctl_ctx *daxctl_ctx;
	struct udev_queue *udev_queue;
	struct kmod_ctx *kmod_ctx;
	struct udev *udev;
	struct cxl_ctx *c;
	int rc = 0;

	c = calloc(1, sizeof(struct cxl_ctx));
	if (!c)
		return -ENOMEM;

	rc = daxctl_new(&daxctl_ctx);
	if (rc)
		goto err_daxctl;

	kmod_ctx = kmod_new(NULL, NULL);
	if (check_kmod(kmod_ctx) != 0) {
		rc = -ENXIO;
		goto err_kmod;
	}

	udev = udev_new();
	if (!udev) {
		rc = -ENOMEM;
		goto err_udev;
	}

	udev_queue = udev_queue_new(udev);
	if (!udev_queue) {
		rc = -ENOMEM;
		goto err_udev_queue;
	}

	c->refcount = 1;
	log_init(&c->ctx, "libcxl", "CXL_LOG");
	info(c, "ctx %p created\n", c);
	dbg(c, "log_priority=%d\n", c->ctx.log_priority);
	*ctx = c;
	list_head_init(&c->memdevs);
	list_head_init(&c->buses);
	c->kmod_ctx = kmod_ctx;
	c->daxctl_ctx = daxctl_ctx;
	c->udev = udev;
	c->udev_queue = udev_queue;
	c->timeout = 5000;

	return 0;

err_udev_queue:
	udev_queue_unref(udev_queue);
err_udev:
	kmod_unref(kmod_ctx);
err_kmod:
	daxctl_unref(daxctl_ctx);
err_daxctl:
	free(c);
	return rc;
}

/**
 * cxl_ref - take an additional reference on the context
 * @ctx: context established by cxl_new()
 */
CXL_EXPORT struct cxl_ctx *cxl_ref(struct cxl_ctx *ctx)
{
	if (ctx == NULL)
		return NULL;
	ctx->refcount++;
	return ctx;
}

/**
 * cxl_unref - drop a context reference count
 * @ctx: context established by cxl_new()
 *
 * Drop a reference and if the resulting reference count is 0 destroy
 * the context.
 */
CXL_EXPORT void cxl_unref(struct cxl_ctx *ctx)
{
	struct cxl_memdev *memdev, *_d;
	struct cxl_bus *bus, *_b;

	if (ctx == NULL)
		return;
	ctx->refcount--;
	if (ctx->refcount > 0)
		return;

	list_for_each_safe(&ctx->memdevs, memdev, _d, list)
		free_memdev(memdev, &ctx->memdevs);

	list_for_each_safe(&ctx->buses, bus, _b, port.list)
		free_bus(bus, &ctx->buses);

	udev_queue_unref(ctx->udev_queue);
	udev_unref(ctx->udev);
	kmod_unref(ctx->kmod_ctx);
	daxctl_unref(ctx->daxctl_ctx);
	info(ctx, "context %p released\n", ctx);
	free(ctx);
}

/**
 * cxl_set_log_fn - override default log routine
 * @ctx: cxl library context
 * @log_fn: function to be called for logging messages
 *
 * The built-in logging writes to stderr. It can be overridden by a
 * custom function, to plug log messages into the user's logging
 * functionality.
 */
CXL_EXPORT void cxl_set_log_fn(struct cxl_ctx *ctx,
		void (*cxl_log_fn)(struct cxl_ctx *ctx, int priority,
			const char *file, int line, const char *fn,
			const char *format, va_list args))
{
	ctx->ctx.log_fn = (log_fn) cxl_log_fn;
	info(ctx, "custom logging function %p registered\n", cxl_log_fn);
}

/**
 * cxl_get_log_priority - retrieve current library loglevel (syslog)
 * @ctx: cxl library context
 */
CXL_EXPORT int cxl_get_log_priority(struct cxl_ctx *ctx)
{
	return ctx->ctx.log_priority;
}

/**
 * cxl_set_log_priority - set log verbosity
 * @priority: from syslog.h, LOG_ERR, LOG_INFO, LOG_DEBUG
 *
 * Note: LOG_DEBUG requires library be built with "configure --enable-debug"
 */
CXL_EXPORT void cxl_set_log_priority(struct cxl_ctx *ctx, int priority)
{
	ctx->ctx.log_priority = priority;
}

static int is_enabled(const char *drvpath)
{
	struct stat st;

	if (lstat(drvpath, &st) < 0 || !S_ISLNK(st.st_mode))
		return 0;
	else
		return 1;
}

CXL_EXPORT int cxl_region_is_enabled(struct cxl_region *region)
{
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	char *path = region->dev_buf;
	int len = region->buf_len;

	if (snprintf(path, len, "%s/driver", region->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", cxl_region_get_devname(region));
		return 0;
	}

	return is_enabled(path);
}

CXL_EXPORT bool cxl_region_qos_class_mismatch(struct cxl_region *region)
{
	struct cxl_decoder *root_decoder = cxl_region_get_decoder(region);
	struct cxl_memdev_mapping *mapping;

	cxl_mapping_foreach(region, mapping) {
		struct cxl_decoder *decoder;
		struct cxl_memdev *memdev;

		decoder = cxl_mapping_get_decoder(mapping);
		if (!decoder)
			continue;

		memdev = cxl_decoder_get_memdev(decoder);
		if (!memdev)
			continue;

		if (region->mode == CXL_DECODER_MODE_RAM) {
			if (root_decoder->qos_class != memdev->ram_qos_class)
				return true;
		} else if (region->mode == CXL_DECODER_MODE_PMEM) {
			if (root_decoder->qos_class != memdev->pmem_qos_class)
				return true;
		}
	}

	return false;
}

CXL_EXPORT int cxl_region_disable(struct cxl_region *region)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);

	util_unbind(region->dev_path, ctx);

	if (cxl_region_is_enabled(region)) {
		err(ctx, "%s: failed to disable\n", devname);
		return -EBUSY;
	}

	dbg(ctx, "%s: disabled\n", devname);

	return 0;
}

CXL_EXPORT int cxl_region_enable(struct cxl_region *region)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	char *path = region->dev_buf;
	int len = region->buf_len;
	char buf[SYSFS_ATTR_SIZE];
	u64 resource = ULLONG_MAX;

	if (cxl_region_is_enabled(region))
		return 0;

	util_bind(devname, region->module, "cxl", ctx);

	if (!cxl_region_is_enabled(region)) {
		err(ctx, "%s: failed to enable\n", devname);
		return -ENXIO;
	}

	/*
	 * Currently 'resource' is the only attr that may change after enabling.
	 * Just refresh it here. If there are additional resources that need
	 * to be refreshed here later, split these out into a common helper
	 * for this and add_cxl_region()
	 */
	if (snprintf(path, len, "%s/resource", region->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return 0;
	}

	if (sysfs_read_attr(ctx, path, buf) == 0)
		resource = strtoull(buf, NULL, 0);

	if (resource < ULLONG_MAX)
		region->start = resource;

	dbg(ctx, "%s: enabled\n", devname);

	return 0;
}

static int cxl_region_delete_name(struct cxl_decoder *decoder,
				  const char *devname)
{
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);
	char *path = decoder->dev_buf;
	int rc;

	sprintf(path, "%s/delete_region", decoder->dev_path);
	rc = sysfs_write_attr(ctx, path, devname);
	if (rc != 0) {
		err(ctx, "error deleting region: %s\n", strerror(-rc));
		return rc;
	}
	return 0;
}

CXL_EXPORT int cxl_region_delete(struct cxl_region *region)
{
	struct cxl_decoder *decoder = cxl_region_get_decoder(region);
	const char *devname = cxl_region_get_devname(region);
	int rc;

	if (cxl_region_is_enabled(region))
		return -EBUSY;

	rc = cxl_region_delete_name(decoder, devname);
	if (rc != 0)
		return rc;

	decoder->regions_init = 0;
	free_region(region, &decoder->regions);
	return 0;
}

static int region_start_cmp(struct cxl_region *r1, struct cxl_region *r2)
{
	if (r1->start == r2->start)
		return 0;
	else if (r1->start < r2->start)
		return -1;
	else
		return 1;
}

static void *add_cxl_region(void *parent, int id, const char *cxlregion_base)
{
	const char *devname = devpath_to_devname(cxlregion_base);
	char *path = calloc(1, strlen(cxlregion_base) + 100);
	struct cxl_region *region, *region_dup, *_r;
	struct cxl_decoder *decoder = parent;
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);
	char buf[SYSFS_ATTR_SIZE];
	u64 resource = ULLONG_MAX;

	dbg(ctx, "%s: base: \'%s\'\n", devname, cxlregion_base);

	if (!path)
		return NULL;

	region = calloc(1, sizeof(*region));
	if (!region)
		goto err_path;

	region->id = id;
	region->ctx = ctx;
	region->decoder = decoder;
	list_head_init(&region->mappings);

	region->dev_path = strdup(cxlregion_base);
	if (!region->dev_path)
		goto err;

	region->dev_buf = calloc(1, strlen(cxlregion_base) + 50);
	if (!region->dev_buf)
		goto err;
	region->buf_len = strlen(cxlregion_base) + 50;

	sprintf(path, "%s/size", cxlregion_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		region->size = ULLONG_MAX;
	else
		region->size = strtoull(buf, NULL, 0);

	sprintf(path, "%s/resource", cxlregion_base);
	if (sysfs_read_attr(ctx, path, buf) == 0)
		resource = strtoull(buf, NULL, 0);

	if (resource < ULLONG_MAX)
		region->start = resource;

	sprintf(path, "%s/uuid", cxlregion_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		goto err;
	if (strlen(buf) && uuid_parse(buf, region->uuid) < 0) {
		dbg(ctx, "%s:%s\n", path, buf);
		goto err;
	}

	sprintf(path, "%s/interleave_granularity", cxlregion_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		region->interleave_granularity = UINT_MAX;
	else
		region->interleave_granularity = strtoul(buf, NULL, 0);

	sprintf(path, "%s/interleave_ways", cxlregion_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		region->interleave_ways = UINT_MAX;
	else
		region->interleave_ways = strtoul(buf, NULL, 0);

	sprintf(path, "%s/commit", cxlregion_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		region->decode_state = CXL_DECODE_UNKNOWN;
	else
		region->decode_state = strtoul(buf, NULL, 0);

	sprintf(path, "%s/mode", cxlregion_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		region->mode = CXL_DECODER_MODE_NONE;
	else
		region->mode = cxl_decoder_mode_from_ident(buf);

	sprintf(path, "%s/modalias", cxlregion_base);
	if (sysfs_read_attr(ctx, path, buf) == 0)
		region->module = util_modalias_to_module(ctx, buf);

	cxl_region_foreach_safe(decoder, region_dup, _r)
		if (region_dup->id == region->id) {
			list_del_from(&decoder->regions, &region_dup->list);
			list_add_tail(&decoder->stale_regions,
				      &region_dup->list);
			break;
		}

	list_add_sorted(&decoder->regions, region, list, region_start_cmp);

	free(path);
	return region;
err:
	free(region->dev_path);
	free(region->dev_buf);
	free(region);
err_path:
	free(path);
	return NULL;
}

static int cxl_flush(struct cxl_ctx *ctx)
{
	return sysfs_write_attr(ctx, "/sys/bus/cxl/flush", "1\n");
}

static int cxl_wait_probe(struct cxl_ctx *ctx)
{
	unsigned long tmo = ctx->timeout;
	int rc, sleep = 0;

	do {
		rc = cxl_flush(ctx);
		if (rc < 0)
			break;
		if (udev_queue_get_queue_is_empty(ctx->udev_queue))
			break;
		sleep++;
		usleep(1000);
	} while (ctx->timeout == 0 || tmo-- != 0);

	if (sleep)
		dbg(ctx, "waited %d millisecond%s...\n", sleep,
		    sleep == 1 ? "" : "s");

	return rc < 0 ? -ENXIO : 0;
}

static int device_parse(struct cxl_ctx *ctx, const char *base_path,
			const char *dev_name, void *parent, add_dev_fn add_dev)
{
	cxl_wait_probe(ctx);
	return sysfs_device_parse(ctx, base_path, dev_name, parent, add_dev);
}

static void cxl_regions_init(struct cxl_decoder *decoder)
{
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);

	if (decoder->regions_init)
		return;

	/* Only root port decoders may have child regions */
	if (!cxl_port_is_root(port))
		return;

	decoder->regions_init = 1;

	device_parse(ctx, decoder->dev_path, "region", decoder, add_cxl_region);
}

CXL_EXPORT struct cxl_region *cxl_region_get_first(struct cxl_decoder *decoder)
{
	cxl_regions_init(decoder);

	return list_top(&decoder->regions, struct cxl_region, list);
}

CXL_EXPORT struct cxl_region *cxl_region_get_next(struct cxl_region *region)
{
	struct cxl_decoder *decoder = region->decoder;

	return list_next(&decoder->regions, region, list);
}

CXL_EXPORT struct cxl_ctx *cxl_region_get_ctx(struct cxl_region *region)
{
	return region->ctx;
}

CXL_EXPORT struct cxl_decoder *cxl_region_get_decoder(struct cxl_region *region)
{
	return region->decoder;
}

CXL_EXPORT int cxl_region_get_id(struct cxl_region *region)
{
	return region->id;
}

CXL_EXPORT const char *cxl_region_get_devname(struct cxl_region *region)
{
	return devpath_to_devname(region->dev_path);
}

CXL_EXPORT void cxl_region_get_uuid(struct cxl_region *region, uuid_t uu)
{
	memcpy(uu, region->uuid, sizeof(uuid_t));
}

CXL_EXPORT unsigned long long cxl_region_get_size(struct cxl_region *region)
{
	return region->size;
}

CXL_EXPORT unsigned long long cxl_region_get_resource(struct cxl_region *region)
{
	return region->start;
}

CXL_EXPORT enum cxl_decoder_mode cxl_region_get_mode(struct cxl_region *region)
{
	return region->mode;
}

CXL_EXPORT unsigned int
cxl_region_get_interleave_ways(struct cxl_region *region)
{
	return region->interleave_ways;
}

CXL_EXPORT int cxl_region_decode_is_committed(struct cxl_region *region)
{
	return (region->decode_state == CXL_DECODE_COMMIT) ? 1 : 0;
}

CXL_EXPORT unsigned int
cxl_region_get_interleave_granularity(struct cxl_region *region)
{
	return region->interleave_granularity;
}

CXL_EXPORT struct cxl_decoder *
cxl_region_get_target_decoder(struct cxl_region *region, int position)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	int len = region->buf_len, rc;
	char *path = region->dev_buf;
	struct cxl_decoder *decoder;
	char buf[SYSFS_ATTR_SIZE];

	if (snprintf(path, len, "%s/target%d", region->dev_path, position) >=
	    len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return NULL;
	}

	rc = sysfs_read_attr(ctx, path, buf);
	if (rc < 0) {
		err(ctx, "%s: error reading target%d: %s\n", devname,
		    position, strerror(-rc));
		return NULL;
	}

	decoder = cxl_decoder_get_by_name(ctx, buf);
	if (!decoder) {
		err(ctx, "%s: error locating decoder for target%d\n", devname,
		    position);
		return NULL;
	}
	return decoder;
}

CXL_EXPORT struct daxctl_region *
cxl_region_get_daxctl_region(struct cxl_region *region)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	char *path = region->dev_buf;
	int len = region->buf_len;
	uuid_t uuid = { 0 };
	struct stat st;

	if (region->dax_region)
		return region->dax_region;

	if (snprintf(region->dev_buf, len, "%s/dax_region%d", region->dev_path,
		     region->id) >= len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return NULL;
	}

	if (stat(path, &st) < 0)
		return NULL;

	region->dax_region =
		daxctl_new_region(ctx->daxctl_ctx, region->id, uuid, path);

	return region->dax_region;
}

CXL_EXPORT int cxl_region_set_size(struct cxl_region *region,
				   unsigned long long size)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	int len = region->buf_len, rc;
	char *path = region->dev_buf;
	char buf[SYSFS_ATTR_SIZE];

	if (size == 0) {
		dbg(ctx, "%s: cannot use %s to delete a region\n", __func__,
		    devname);
		return -EINVAL;
	}

	if (snprintf(path, len, "%s/size", region->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return -ENXIO;
	}

	sprintf(buf, "%#llx\n", size);
	rc = sysfs_write_attr(ctx, path, buf);
	if (rc < 0)
		return rc;

	region->size = size;

	return 0;
}

CXL_EXPORT int cxl_region_set_uuid(struct cxl_region *region, uuid_t uu)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	int len = region->buf_len, rc;
	char *path = region->dev_buf;
	char uuid[SYSFS_ATTR_SIZE];

	if (snprintf(path, len, "%s/uuid", region->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return -ENXIO;
	}

	uuid_unparse(uu, uuid);
	rc = sysfs_write_attr(ctx, path, uuid);
	if (rc != 0)
		return rc;
	memcpy(region->uuid, uu, sizeof(uuid_t));
	return 0;
}

CXL_EXPORT int cxl_region_set_interleave_ways(struct cxl_region *region,
					      unsigned int ways)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	int len = region->buf_len, rc;
	char *path = region->dev_buf;
	char buf[SYSFS_ATTR_SIZE];

	if (snprintf(path, len, "%s/interleave_ways",
		     region->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return -ENXIO;
	}

	sprintf(buf, "%u\n", ways);
	rc = sysfs_write_attr(ctx, path, buf);
	if (rc < 0)
		return rc;

	region->interleave_ways = ways;

	return 0;
}

CXL_EXPORT int cxl_region_set_interleave_granularity(struct cxl_region *region,
						     unsigned int granularity)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	int len = region->buf_len, rc;
	char *path = region->dev_buf;
	char buf[SYSFS_ATTR_SIZE];

	if (snprintf(path, len, "%s/interleave_granularity",
		     region->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return -ENXIO;
	}

	sprintf(buf, "%u\n", granularity);
	rc = sysfs_write_attr(ctx, path, buf);
	if (rc < 0)
		return rc;

	region->interleave_granularity = granularity;

	return 0;
}

static int region_write_target(struct cxl_region *region, int position,
			       struct cxl_decoder *decoder)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	int len = region->buf_len, rc;
	char *path = region->dev_buf;
	const char *dec_name = "";

	if (decoder)
		dec_name = cxl_decoder_get_devname(decoder);

	if (snprintf(path, len, "%s/target%d", region->dev_path, position) >=
	    len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return -ENXIO;
	}

	rc = sysfs_write_attr(ctx, path, dec_name);
	if (rc < 0)
		return rc;

	return 0;
}

CXL_EXPORT int cxl_region_set_target(struct cxl_region *region, int position,
				     struct cxl_decoder *decoder)
{
	if (!decoder)
		return -ENXIO;

	return region_write_target(region, position, decoder);
}

CXL_EXPORT int cxl_region_clear_target(struct cxl_region *region, int position)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	int rc;

	if (cxl_region_is_enabled(region)) {
		err(ctx, "%s: can't clear targets on an active region\n",
		    devname);
		return -EBUSY;
	}

	rc = region_write_target(region, position, NULL);
	if (rc) {
		err(ctx, "%s: error clearing target%d: %s\n",
		    devname, position, strerror(-rc));
		return rc;
	}

	return 0;
}

CXL_EXPORT int cxl_region_clear_all_targets(struct cxl_region *region)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	unsigned int ways, i;
	int rc;

	if (cxl_region_is_enabled(region)) {
		err(ctx, "%s: can't clear targets on an active region\n",
		    devname);
		return -EBUSY;
	}

	ways = cxl_region_get_interleave_ways(region);
	if (ways == 0 || ways == UINT_MAX)
		return -ENXIO;

	for (i = 0; i < ways; i++) {
		rc = region_write_target(region, i, NULL);
		if (rc) {
			err(ctx, "%s: error clearing target%d: %s\n",
			    devname, i, strerror(-rc));
			return rc;
		}
	}

	return 0;
}

static int set_region_decode(struct cxl_region *region,
			     enum cxl_decode_state decode_state)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	int len = region->buf_len, rc;
	char *path = region->dev_buf;
	char buf[SYSFS_ATTR_SIZE];

	if (snprintf(path, len, "%s/commit", region->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return -ENXIO;
	}

	sprintf(buf, "%d\n", decode_state);
	rc = sysfs_write_attr(ctx, path, buf);
	if (rc < 0)
		return rc;

	region->decode_state = decode_state;

	return 0;
}

CXL_EXPORT int cxl_region_decode_commit(struct cxl_region *region)
{
	return set_region_decode(region, CXL_DECODE_COMMIT);
}

CXL_EXPORT int cxl_region_decode_reset(struct cxl_region *region)
{
	return set_region_decode(region, CXL_DECODE_RESET);
}

static struct cxl_decoder *__cxl_port_match_decoder(struct cxl_port *port,
						    const char *ident)
{
	struct cxl_decoder *decoder;

	cxl_decoder_foreach(port, decoder)
		if (strcmp(cxl_decoder_get_devname(decoder), ident) == 0)
			return decoder;

	return NULL;
}

static struct cxl_decoder *cxl_port_find_decoder(struct cxl_port *port,
						 const char *ident)
{
	struct cxl_decoder *decoder;
	struct cxl_endpoint *ep;

	/* First, check decoders directly under @port */
	decoder = __cxl_port_match_decoder(port, ident);
	if (decoder)
		return decoder;

	/* Next, iterate over the endpoints under @port */
	cxl_endpoint_foreach(port, ep) {
		decoder = __cxl_port_match_decoder(cxl_endpoint_get_port(ep),
						   ident);
		if (decoder)
			return decoder;
	}

	return NULL;
}

CXL_EXPORT struct cxl_decoder *cxl_decoder_get_by_name(struct cxl_ctx *ctx,
						       const char *ident)
{
	struct cxl_bus *bus;

	cxl_bus_foreach(ctx, bus) {
		struct cxl_decoder *decoder;
		struct cxl_port *port, *top;

		port = cxl_bus_get_port(bus);
		decoder = cxl_port_find_decoder(port, ident);
		if (decoder)
			return decoder;

		top = port;
		cxl_port_foreach_all (top, port) {
			decoder = cxl_port_find_decoder(port, ident);
			if (decoder)
				return decoder;
		}
	}

	return NULL;
}

static void cxl_mappings_init(struct cxl_region *region)
{
	const char *devname = cxl_region_get_devname(region);
	struct cxl_ctx *ctx = cxl_region_get_ctx(region);
	char *mapping_path, buf[SYSFS_ATTR_SIZE];
	unsigned int i;

	if (region->mappings_init)
		return;
	region->mappings_init = 1;

	mapping_path = calloc(1, strlen(region->dev_path) + 100);
	if (!mapping_path) {
		err(ctx, "%s: allocation failure\n", devname);
		return;
	}

	for (i = 0; i < region->interleave_ways; i++) {
		struct cxl_memdev_mapping *mapping;
		struct cxl_decoder *decoder;

		sprintf(mapping_path, "%s/target%d", region->dev_path, i);
		if (sysfs_read_attr(ctx, mapping_path, buf) < 0) {
			err(ctx, "%s: failed to read target%d\n", devname, i);
			continue;
		}

		decoder = cxl_decoder_get_by_name(ctx, buf);
		if (!decoder) {
			err(ctx, "%s target%d: %s lookup failure\n",
			    devname, i, buf);
			continue;
		}

		mapping = calloc(1, sizeof(*mapping));
		if (!mapping) {
			err(ctx, "%s target%d: allocation failure\n", devname, i);
			continue;
		}

		mapping->region = region;
		mapping->decoder = decoder;
		mapping->position = i;
		list_add(&region->mappings, &mapping->list);
	}
	free(mapping_path);
}

CXL_EXPORT struct cxl_memdev_mapping *
cxl_mapping_get_first(struct cxl_region *region)
{
	cxl_mappings_init(region);

	return list_top(&region->mappings, struct cxl_memdev_mapping, list);
}

CXL_EXPORT struct cxl_memdev_mapping *
cxl_mapping_get_next(struct cxl_memdev_mapping *mapping)
{
	struct cxl_region *region = mapping->region;

	return list_next(&region->mappings, mapping, list);
}

CXL_EXPORT struct cxl_decoder *
cxl_mapping_get_decoder(struct cxl_memdev_mapping *mapping)
{
	return mapping->decoder;
}

CXL_EXPORT unsigned int
cxl_mapping_get_position(struct cxl_memdev_mapping *mapping)
{
	return mapping->position;
}

static void *add_cxl_pmem(void *parent, int id, const char *br_base)
{
	const char *devname = devpath_to_devname(br_base);
	struct cxl_memdev *memdev = parent;
	struct cxl_ctx *ctx = memdev->ctx;
	struct cxl_pmem *pmem;

	dbg(ctx, "%s: pmem_base: \'%s\'\n", devname, br_base);

	pmem = calloc(1, sizeof(*pmem));
	if (!pmem)
		goto err_dev;
	pmem->id = id;

	pmem->dev_path = strdup(br_base);
	if (!pmem->dev_path)
		goto err_read;

	pmem->dev_buf = calloc(1, strlen(br_base) + 50);
	if (!pmem->dev_buf)
		goto err_read;
	pmem->buf_len = strlen(br_base) + 50;

	memdev->pmem = pmem;
	return pmem;

 err_read:
	free(pmem->dev_buf);
	free(pmem->dev_path);
	free(pmem);
 err_dev:
	return NULL;
}

static int add_cxl_memdev_fwl(struct cxl_memdev *memdev,
			      const char *cxlmem_base)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_fw_loader *fwl;

	fwl = calloc(1, sizeof(*fwl));
	if (!fwl)
		return -ENOMEM;

	if (asprintf(&fwl->loading, "%s/firmware/%s/loading", cxlmem_base,
		     devname) < 0)
		goto err_read;
	if (asprintf(&fwl->data, "%s/firmware/%s/data", cxlmem_base, devname) <
	    0)
		goto err_read;
	if (asprintf(&fwl->remaining, "%s/firmware/%s/remaining_size",
		     cxlmem_base, devname) < 0)
		goto err_read;
	if (asprintf(&fwl->cancel, "%s/firmware/%s/cancel", cxlmem_base,
		     devname) < 0)
		goto err_read;
	if (asprintf(&fwl->status, "%s/firmware/%s/status", cxlmem_base,
		     devname) < 0)
		goto err_read;

	memdev->fwl = fwl;
	return 0;

 err_read:
	free_fwl(fwl);
	return -ENOMEM;
}

static void *add_cxl_memdev(void *parent, int id, const char *cxlmem_base)
{
	const char *devname = devpath_to_devname(cxlmem_base);
	char *path = calloc(1, strlen(cxlmem_base) + 100);
	struct cxl_ctx *ctx = parent;
	struct cxl_memdev *memdev, *memdev_dup;
	char buf[SYSFS_ATTR_SIZE];
	struct stat st;
	char *host;

	if (!path)
		return NULL;
	dbg(ctx, "%s: base: \'%s\'\n", devname, cxlmem_base);

	memdev = calloc(1, sizeof(*memdev));
	if (!memdev)
		goto err_dev;
	memdev->id = id;
	memdev->ctx = ctx;

	sprintf(path, "/dev/cxl/%s", devname);
	if (stat(path, &st) < 0)
		goto err_read;
	memdev->major = major(st.st_rdev);
	memdev->minor = minor(st.st_rdev);

	sprintf(path, "%s/pmem/size", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) == 0)
		memdev->pmem_size = strtoull(buf, NULL, 0);

	sprintf(path, "%s/ram/size", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) == 0)
		memdev->ram_size = strtoull(buf, NULL, 0);

	sprintf(path, "%s/pmem/qos_class", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		memdev->pmem_qos_class = CXL_QOS_CLASS_NONE;
	else
		memdev->pmem_qos_class = atoi(buf);

	sprintf(path, "%s/ram/qos_class", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		memdev->ram_qos_class = CXL_QOS_CLASS_NONE;
	else
		memdev->ram_qos_class = atoi(buf);

	sprintf(path, "%s/payload_max", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) == 0) {
		memdev->payload_max = strtoull(buf, NULL, 0);
		if (memdev->payload_max < 0)
			goto err_read;
	} else {
		memdev->payload_max = -1;
	}

	sprintf(path, "%s/label_storage_size", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) == 0) {
		memdev->lsa_size = strtoull(buf, NULL, 0);
		if (memdev->lsa_size == ULLONG_MAX)
			goto err_read;
	} else {
		memdev->lsa_size = SIZE_MAX;
	}

	sprintf(path, "%s/serial", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		memdev->serial = ULLONG_MAX;
	else
		memdev->serial = strtoull(buf, NULL, 0);

	sprintf(path, "%s/numa_node", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		memdev->numa_node = -1;
	else
		memdev->numa_node = strtol(buf, NULL, 0);

	memdev->dev_path = strdup(cxlmem_base);
	if (!memdev->dev_path)
		goto err_read;

	memdev->host_path = realpath(cxlmem_base, NULL);
	if (!memdev->host_path)
		goto err_read;
	host = strrchr(memdev->host_path, '/');
	if (!host)
		goto err_read;
	host[0] = '\0';

	sprintf(path, "%s/firmware_version", cxlmem_base);
	if (sysfs_read_attr(ctx, path, buf) == 0) {
		memdev->firmware_version = strdup(buf);
		if (!memdev->firmware_version)
			goto err_read;
	}

	memdev->dev_buf = calloc(1, strlen(cxlmem_base) + 50);
	if (!memdev->dev_buf)
		goto err_read;
	memdev->buf_len = strlen(cxlmem_base) + 50;

	device_parse(ctx, cxlmem_base, "pmem", memdev, add_cxl_pmem);

	if (add_cxl_memdev_fwl(memdev, cxlmem_base))
		goto err_read;

	cxl_memdev_foreach(ctx, memdev_dup)
		if (memdev_dup->id == memdev->id) {
			free_memdev(memdev, NULL);
			free(path);
			return memdev_dup;
		}

	list_add(&ctx->memdevs, &memdev->list);
	free(path);
	return memdev;

 err_read:
	free(memdev->firmware_version);
	free(memdev->dev_buf);
	free(memdev->dev_path);
	free(memdev->host_path);
	free(memdev);
 err_dev:
	free(path);
	return NULL;
}

static void cxl_memdevs_init(struct cxl_ctx *ctx)
{
	if (ctx->memdevs_init)
		return;

	ctx->memdevs_init = 1;

	device_parse(ctx, "/sys/bus/cxl/devices", "mem", ctx, add_cxl_memdev);
}

CXL_EXPORT struct cxl_ctx *cxl_memdev_get_ctx(struct cxl_memdev *memdev)
{
	return memdev->ctx;
}

CXL_EXPORT struct cxl_memdev *cxl_memdev_get_first(struct cxl_ctx *ctx)
{
	cxl_memdevs_init(ctx);

	return list_top(&ctx->memdevs, struct cxl_memdev, list);
}

CXL_EXPORT struct cxl_memdev *cxl_memdev_get_next(struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = memdev->ctx;

	return list_next(&ctx->memdevs, memdev, list);
}

CXL_EXPORT int cxl_memdev_get_id(struct cxl_memdev *memdev)
{
	return memdev->id;
}

CXL_EXPORT int cxl_memdev_wait_sanitize(struct cxl_memdev *memdev,
					int timeout_ms)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	const char *devname = cxl_memdev_get_devname(memdev);
	char *path = memdev->dev_buf;
	int len = memdev->buf_len;
	struct pollfd fds = { 0 };
	int fd = 0, rc;
	char buf[9];

	if (snprintf(path, len, "%s/security/state", memdev->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", devname);
		return -ERANGE;
	}

	fd = open(path, O_RDONLY|O_CLOEXEC);
	if (fd < 0) {
		/* device does not support security operations */
		if (errno == ENOENT)
			return 0;
		rc = -errno;
		err(ctx, "%s: %s\n", path, strerror(errno));
		return rc;
	}
	memset(&fds, 0, sizeof(fds));
	fds.fd = fd;

	rc = pread(fd, buf, sizeof(buf), 0);
	if (rc < 0) {
		rc = -EOPNOTSUPP;
		goto out;
	}

	/* skipping if not currently sanitizing */
	if (strncmp(buf, "sanitize", 8) != 0) {
		rc = 0;
		goto out;
	}

	rc = poll(&fds, 1, timeout_ms);
	if (rc == 0) {
		dbg(ctx, "%s: sanitize timeout\n", devname);
		rc = -ETIMEDOUT;
	} else if (rc < 0) {
		err(ctx, "%s: sanitize poll error: %s\n", devname, strerror(errno));
		rc = -errno;
	} else {
		dbg(ctx, "%s: sanitize wake\n", devname);

		rc = pread(fd, buf, sizeof(buf), 0);
		if (rc < 0 || strncmp(buf, "sanitize", 8) == 0) {
			err(ctx, "%s: sanitize wake error\n", devname);
			rc = -ENXIO;
		} else
			rc = 0;
	}
out:
	close(fd);
	return rc;
}

CXL_EXPORT unsigned long long cxl_memdev_get_serial(struct cxl_memdev *memdev)
{
	return memdev->serial;
}

CXL_EXPORT int cxl_memdev_get_numa_node(struct cxl_memdev *memdev)
{
	return memdev->numa_node;
}

CXL_EXPORT const char *cxl_memdev_get_devname(struct cxl_memdev *memdev)
{
	return devpath_to_devname(memdev->dev_path);
}

CXL_EXPORT const char *cxl_memdev_get_host(struct cxl_memdev *memdev)
{
	return devpath_to_devname(memdev->host_path);
}

CXL_EXPORT struct cxl_bus *cxl_memdev_get_bus(struct cxl_memdev *memdev)
{
	struct cxl_endpoint *endpoint = cxl_memdev_get_endpoint(memdev);

	if (!endpoint)
		return NULL;
	return cxl_endpoint_get_bus(endpoint);
}

CXL_EXPORT int cxl_memdev_get_major(struct cxl_memdev *memdev)
{
	return memdev->major;
}

CXL_EXPORT int cxl_memdev_get_minor(struct cxl_memdev *memdev)
{
	return memdev->minor;
}

CXL_EXPORT unsigned long long cxl_memdev_get_pmem_size(struct cxl_memdev *memdev)
{
	return memdev->pmem_size;
}

CXL_EXPORT unsigned long long cxl_memdev_get_ram_size(struct cxl_memdev *memdev)
{
	return memdev->ram_size;
}

CXL_EXPORT int cxl_memdev_get_pmem_qos_class(struct cxl_memdev *memdev)
{
	return memdev->pmem_qos_class;
}

CXL_EXPORT int cxl_memdev_get_ram_qos_class(struct cxl_memdev *memdev)
{
	return memdev->ram_qos_class;
}

CXL_EXPORT const char *cxl_memdev_get_firmware_verison(struct cxl_memdev *memdev)
{
	return memdev->firmware_version;
}

static enum cxl_fwl_status cxl_fwl_get_status(struct cxl_memdev *memdev)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_fw_loader *fwl = memdev->fwl;
	char buf[SYSFS_ATTR_SIZE];
	int rc;

	rc = sysfs_read_attr(ctx, fwl->status, buf);
	if (rc < 0) {
		err(ctx, "%s: failed to get fw loader status (%s)\n", devname,
		    strerror(-rc));
		return CXL_FWL_STATUS_UNKNOWN;
	}

	return cxl_fwl_status_from_ident(buf);
}

CXL_EXPORT bool cxl_memdev_fw_update_in_progress(struct cxl_memdev *memdev)
{
	int status = cxl_fwl_get_status(memdev);

	if (status == CXL_FWL_STATUS_IDLE)
		return false;
	return true;
}

CXL_EXPORT size_t cxl_memdev_fw_update_get_remaining(struct cxl_memdev *memdev)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_fw_loader *fwl = memdev->fwl;
	char buf[SYSFS_ATTR_SIZE];
	int rc;

	rc = sysfs_read_attr(ctx, fwl->remaining, buf);
	if (rc < 0) {
		err(ctx, "%s: failed to get fw loader remaining size (%s)\n",
		    devname, strerror(-rc));
		return 0;
	}

	return strtoull(buf, NULL, 0);
}

static int cxl_memdev_fwl_set_loading(struct cxl_memdev *memdev,
				      enum cxl_fwl_loading loadval)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_fw_loader *fwl = memdev->fwl;
	char buf[SYSFS_ATTR_SIZE];
	int rc;

	sprintf(buf, "%d\n", loadval);
	rc = sysfs_write_attr(ctx, fwl->loading, buf);
	if (rc < 0) {
		err(ctx, "%s: failed to trigger fw loading to %d (%s)\n",
		    devname, loadval, strerror(-rc));
		return rc;
	}

	return 0;
}

static int cxl_memdev_fwl_copy_data(struct cxl_memdev *memdev, void *fw_buf,
				    size_t size)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_fw_loader *fwl = memdev->fwl;
	FILE *fwl_data;
	size_t rw_len;
	int rc = 0;

	fwl_data = fopen(fwl->data, "w");
	if (!fwl_data) {
		err(ctx, "failed to open: %s: (%s)\n", fwl->data,
		    strerror(errno));
		return -errno;
	}

	rw_len = fwrite(fw_buf, 1, size, fwl_data);
	if (rw_len != size) {
		rc = -ENXIO;
		goto out_close;
	}
	fflush(fwl_data);

out_close:
	fclose(fwl_data);
	return rc;
}

CXL_EXPORT int cxl_memdev_update_fw(struct cxl_memdev *memdev,
				    const char *fw_path)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct stat s;
	int f_in, rc;
	void *fw_buf;

	f_in = open(fw_path, O_RDONLY);
	if (f_in < 0) {
		err(ctx, "failed to open: %s: (%s)\n", fw_path,
		    strerror(errno));
		return -errno;
	}

	rc = fstat(f_in, &s);
	if (rc < 0) {
		err(ctx, "failed to stat: %s: (%s)\n", fw_path,
		    strerror(errno));
		rc = -errno;
		goto out_close;
	}

	fw_buf = mmap(NULL, s.st_size, PROT_READ, MAP_PRIVATE, f_in, 0);
	if (fw_buf == MAP_FAILED) {
		err(ctx, "failed to map: %s: (%s)\n", fw_path,
		    strerror(errno));
		rc = -errno;
		goto out_close;
	}

	rc = cxl_memdev_fwl_set_loading(memdev, CXL_FWL_LOADING_START);
	if (rc)
		goto out_unmap;

	rc = cxl_memdev_fwl_copy_data(memdev, fw_buf, s.st_size);
	if (rc)
		goto out_unmap;

	rc = cxl_memdev_fwl_set_loading(memdev, CXL_FWL_LOADING_END);

out_unmap:
	munmap(fw_buf, s.st_size);
out_close:
	close(f_in);
	return rc;
}

CXL_EXPORT int cxl_memdev_cancel_fw_update(struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_fw_loader *fwl = memdev->fwl;
	int rc;

	if (!cxl_memdev_fw_update_in_progress(memdev) &&
	    cxl_memdev_fw_update_get_remaining(memdev) == 0)
		return -ENXIO;

	rc = sysfs_write_attr(ctx, fwl->cancel, "1\n");
	if (rc < 0)
		return rc;

	return 0;
}

static void bus_invalidate(struct cxl_bus *bus)
{
	struct cxl_ctx *ctx = cxl_bus_get_ctx(bus);
	struct cxl_port *bus_port, *port, *_p;
	struct cxl_memdev *memdev;

	/*
	 * Something happend to cause the state of all ports to be
	 * indeterminate, delete them all and start over.
	 */
	cxl_memdev_foreach(ctx, memdev)
		memdev->endpoint = NULL;

	bus_port = cxl_bus_get_port(bus);
	list_for_each_safe(&bus_port->child_ports, port, _p, list)
		free_port(port, &bus_port->child_ports);
	bus_port->ports_init = 0;
	cxl_flush(ctx);
}

CXL_EXPORT int cxl_bus_disable_invalidate(struct cxl_bus *bus)
{
	struct cxl_ctx *ctx = cxl_bus_get_ctx(bus);
	struct cxl_port *port = cxl_bus_get_port(bus);
	int rc;

	rc = util_unbind(port->uport, ctx);
	if (rc)
		return rc;

	free_bus(bus, &ctx->buses);
	cxl_flush(ctx);
	return 0;
}

CXL_EXPORT int cxl_memdev_disable_invalidate(struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_bus *bus;

	if (!cxl_memdev_is_enabled(memdev))
		return 0;

	bus = cxl_memdev_get_bus(memdev);
	if (!bus) {
		err(ctx, "%s: failed to invalidate\n", devname);
		return -ENXIO;
	}

	util_unbind(memdev->dev_path, ctx);

	if (cxl_memdev_is_enabled(memdev)) {
		err(ctx, "%s: failed to disable\n", devname);
		return -EBUSY;
	}

	bus_invalidate(bus);

	dbg(ctx, "%s: disabled\n", devname);

	return 0;
}

CXL_EXPORT int cxl_memdev_enable(struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	const char *devname = cxl_memdev_get_devname(memdev);

	if (cxl_memdev_is_enabled(memdev))
		return 0;

	util_bind(devname, memdev->module, "cxl", ctx);

	if (!cxl_memdev_is_enabled(memdev)) {
		err(ctx, "%s: failed to enable\n", devname);
		return -ENXIO;
	}

	dbg(ctx, "%s: enabled\n", devname);

	return 0;
}

static struct cxl_endpoint *
cxl_port_recurse_endpoint(struct cxl_port *parent_port,
			  struct cxl_memdev *memdev)
{
	struct cxl_endpoint *endpoint;
	struct cxl_port *port;

	cxl_port_foreach(parent_port, port) {
		cxl_endpoint_foreach(port, endpoint)
			if (strcmp(cxl_endpoint_get_host(endpoint),
				   cxl_memdev_get_devname(memdev)) == 0)
				return endpoint;
		endpoint = cxl_port_recurse_endpoint(port, memdev);
		if (endpoint)
			return endpoint;
	}

	return NULL;
}

static struct cxl_endpoint *cxl_port_find_endpoint(struct cxl_port *parent_port,
						   struct cxl_memdev *memdev)
{
	struct cxl_endpoint *endpoint;

	cxl_endpoint_foreach(parent_port, endpoint)
		if (strcmp(cxl_endpoint_get_host(endpoint),
			   cxl_memdev_get_devname(memdev)) == 0)
			return endpoint;
	return cxl_port_recurse_endpoint(parent_port, memdev);
}

CXL_EXPORT struct cxl_endpoint *
cxl_memdev_get_endpoint(struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_endpoint *endpoint = NULL;
	struct cxl_bus *bus;

	if (memdev->endpoint)
		return memdev->endpoint;

	if (!cxl_memdev_is_enabled(memdev))
		return NULL;

	cxl_bus_foreach (ctx, bus) {
		struct cxl_port *port = cxl_bus_get_port(bus);

		endpoint = cxl_port_find_endpoint(port, memdev);
		if (endpoint)
			break;
	}

	if (!endpoint)
		return NULL;

	if (endpoint->memdev && endpoint->memdev != memdev)
		err(ctx, "%s assigned to %s not %s\n",
		    cxl_endpoint_get_devname(endpoint),
		    cxl_memdev_get_devname(endpoint->memdev),
		    cxl_memdev_get_devname(memdev));
	memdev->endpoint = endpoint;
	endpoint->memdev = memdev;

	return endpoint;
}

CXL_EXPORT size_t cxl_memdev_get_label_size(struct cxl_memdev *memdev)
{
	return memdev->lsa_size;
}

CXL_EXPORT int cxl_memdev_is_enabled(struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	char *path = memdev->dev_buf;
	int len = memdev->buf_len;

	if (snprintf(path, len, "%s/driver", memdev->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n",
		    cxl_memdev_get_devname(memdev));
		return 0;
	}

	return is_enabled(path);
}

CXL_EXPORT int cxl_memdev_nvdimm_bridge_active(struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_pmem *pmem = memdev->pmem;
	char *path;
	int len;

	if (!pmem)
		return 0;

	path = pmem->dev_buf;
	len = pmem->buf_len;

	if (snprintf(path, len, "%s/driver", pmem->dev_path) >= len) {
		err(ctx, "%s: nvdimm pmem buffer too small!\n",
				cxl_memdev_get_devname(memdev));
		return 0;
	}

	return is_enabled(path);
}

static int cxl_port_init(struct cxl_port *port, struct cxl_port *parent_port,
			 enum cxl_port_type type, struct cxl_ctx *ctx, int id,
			 const char *cxlport_base)
{
	char *path = calloc(1, strlen(cxlport_base) + 100);
	char buf[SYSFS_ATTR_SIZE];
	size_t rc;

	if (!path)
		return -ENOMEM;

	port->id = id;
	port->ctx = ctx;
	port->type = type;
	port->parent = parent_port;
	port->type = type;
	port->depth = parent_port ? parent_port->depth + 1 : 0;

	list_head_init(&port->child_ports);
	list_head_init(&port->endpoints);
	list_head_init(&port->decoders);
	list_head_init(&port->dports);

	port->dev_path = strdup(cxlport_base);
	if (!port->dev_path)
		goto err;

	port->dev_buf = calloc(1, strlen(cxlport_base) + 50);
	if (!port->dev_buf)
		goto err;
	port->buf_len = strlen(cxlport_base) + 50;

	rc = snprintf(port->dev_buf, port->buf_len, "%s/uport", cxlport_base);
	if (rc >= port->buf_len)
		goto err;
	port->uport = realpath(port->dev_buf, NULL);
	if (!port->uport)
		goto err;

	/*
	 * CXL root devices have no parents and level 1 ports are both
	 * CXL root targets and hosts of the next level, so:
	 *     parent_dport == uport
	 * ...at depth == 1
	 */
	if (port->depth > 1) {
		rc = snprintf(port->dev_buf, port->buf_len, "%s/parent_dport",
			      cxlport_base);
		if (rc >= port->buf_len)
			goto err;
		port->parent_dport_path = realpath(port->dev_buf, NULL);
	}

	sprintf(path, "%s/modalias", cxlport_base);
	if (sysfs_read_attr(ctx, path, buf) == 0)
		port->module = util_modalias_to_module(ctx, buf);

	sprintf(path, "%s/decoders_committed", cxlport_base);
	if (sysfs_read_attr(ctx, path, buf) == 0)
		port->decoders_committed = strtoul(buf, NULL, 0);

	free(path);
	return 0;
err:
	free(port->dev_path);
	free(port->dev_buf);
	free(path);
	return -ENOMEM;
}

static void *add_cxl_endpoint(void *parent, int id, const char *cxlep_base)
{
	const char *devname = devpath_to_devname(cxlep_base);
	struct cxl_endpoint *endpoint, *endpoint_dup;
	struct cxl_port *port = parent;
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);
	int rc;

	dbg(ctx, "%s: base: \'%s\'\n", devname, cxlep_base);

	endpoint = calloc(1, sizeof(*endpoint));
	if (!endpoint)
		return NULL;

	rc = cxl_port_init(&endpoint->port, port, CXL_PORT_ENDPOINT, ctx, id,
			   cxlep_base);
	if (rc)
		goto err;

	cxl_endpoint_foreach(port, endpoint_dup)
		if (endpoint_dup->port.id == endpoint->port.id) {
			free_endpoint(endpoint, NULL);
			return endpoint_dup;
		}

	list_add(&port->endpoints, &endpoint->port.list);
	return endpoint;

err:
	free(endpoint);
	return NULL;

}

static void cxl_endpoints_init(struct cxl_port *port)
{
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);

	if (port->endpoints_init)
		return;

	port->endpoints_init = 1;

	device_parse(ctx, port->dev_path, "endpoint", port, add_cxl_endpoint);
}

CXL_EXPORT struct cxl_ctx *cxl_endpoint_get_ctx(struct cxl_endpoint *endpoint)
{
	return endpoint->port.ctx;
}

CXL_EXPORT struct cxl_endpoint *cxl_endpoint_get_first(struct cxl_port *port)
{
	cxl_endpoints_init(port);

	return list_top(&port->endpoints, struct cxl_endpoint, port.list);
}

CXL_EXPORT struct cxl_endpoint *cxl_endpoint_get_next(struct cxl_endpoint *endpoint)
{
	struct cxl_port *port = endpoint->port.parent;

	return list_next(&port->endpoints, endpoint, port.list);
}

CXL_EXPORT const char *cxl_endpoint_get_devname(struct cxl_endpoint *endpoint)
{
	return devpath_to_devname(endpoint->port.dev_path);
}

CXL_EXPORT int cxl_endpoint_get_id(struct cxl_endpoint *endpoint)
{
	return endpoint->port.id;
}

CXL_EXPORT struct cxl_port *cxl_endpoint_get_parent(struct cxl_endpoint *endpoint)
{
	return endpoint->port.parent;
}

CXL_EXPORT struct cxl_port *cxl_endpoint_get_port(struct cxl_endpoint *endpoint)
{
	return &endpoint->port;
}

CXL_EXPORT const char *cxl_endpoint_get_host(struct cxl_endpoint *endpoint)
{
	return cxl_port_get_host(&endpoint->port);
}

CXL_EXPORT struct cxl_bus *cxl_endpoint_get_bus(struct cxl_endpoint *endpoint)
{
	struct cxl_port *port = &endpoint->port;

	return cxl_port_get_bus(port);
}

CXL_EXPORT int cxl_endpoint_is_enabled(struct cxl_endpoint *endpoint)
{
	return cxl_port_is_enabled(&endpoint->port);
}

CXL_EXPORT struct cxl_memdev *
cxl_endpoint_get_memdev(struct cxl_endpoint *endpoint)
{
	struct cxl_ctx *ctx = cxl_endpoint_get_ctx(endpoint);
	struct cxl_memdev *memdev;

	if (endpoint->memdev)
		return endpoint->memdev;

	if (!cxl_endpoint_is_enabled(endpoint))
		return NULL;

	cxl_memdev_foreach(ctx, memdev)
		if (strcmp(cxl_memdev_get_devname(memdev),
			   cxl_endpoint_get_host(endpoint)) == 0) {
			if (memdev->endpoint && memdev->endpoint != endpoint)
				err(ctx, "%s assigned to %s not %s\n",
				    cxl_memdev_get_devname(memdev),
				    cxl_endpoint_get_devname(memdev->endpoint),
				    cxl_endpoint_get_devname(endpoint));
			endpoint->memdev = memdev;
			memdev->endpoint = endpoint;
			return memdev;
		}

	return NULL;
}

static bool cxl_region_is_configured(struct cxl_region *region)
{
	return region->size && (region->decode_state != CXL_DECODE_RESET);
}

/**
 * cxl_decoder_calc_max_available_extent() - calculate max available free space
 * @decoder - the root decoder to calculate the free extents for
 *
 * The add_cxl_region() function  adds regions to the parent decoder's list
 * sorted by the region's start HPAs. It can also be assumed that regions have
 * no overlapped / aliased HPA space. Therefore, calculating each extent is as
 * simple as walking the region list in order, and subtracting the previous
 * region's end HPA from the next region's start HPA (and taking into account
 * the decoder's start and end HPAs as well).
 */
static unsigned long long
cxl_decoder_calc_max_available_extent(struct cxl_decoder *decoder)
{
	u64 prev_end, decoder_end, cur_extent, max_extent = 0;
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);
	struct cxl_region *region;

	if (!cxl_port_is_root(port)) {
		err(ctx, "%s: not a root decoder\n",
		    cxl_decoder_get_devname(decoder));
		return ULLONG_MAX;
	}

	if (decoder->start == ULLONG_MAX)
		return ULLONG_MAX;

	/*
	 * Preload prev_end with an imaginary region that ends just before
	 * the decoder's start, so that the extent calculation for the
	 * first region Just Works
	 */
	prev_end = decoder->start - 1;

	cxl_region_foreach(decoder, region) {
		if (!cxl_region_is_configured(region))
			continue;

		/*
		 * region->start - prev_end would get the difference in
		 * addresses, but a difference of 1 in addresses implies
		 * an extent of 0. Hence the '-1'.
		 */
		cur_extent = region->start - prev_end - 1;
		max_extent = max(max_extent, cur_extent);
		prev_end = region->start + region->size - 1;
	}

	/*
	 * Finally, consider the extent after the last region, up to the end
	 * of the decoder's address space, if any. If there were no regions,
	 * this simply reduces to decoder->size.
	 * Subtracting two addrs gets us a 'size' directly, no need for +/- 1.
	 */
	decoder_end = decoder->start + decoder->size - 1;
	cur_extent = decoder_end - prev_end;
	max_extent = max(max_extent, cur_extent);

	return max_extent;
}

static int decoder_id_cmp(struct cxl_decoder *d1, struct cxl_decoder *d2)
{
	return d1->id - d2->id;
}

static void *add_cxl_decoder(void *parent, int id, const char *cxldecoder_base)
{
	const char *devname = devpath_to_devname(cxldecoder_base);
	char *path = calloc(1, strlen(cxldecoder_base) + 100);
	struct cxl_decoder *decoder, *decoder_dup;
	struct cxl_port *port = parent;
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);
	char buf[SYSFS_ATTR_SIZE];
	char *target_id, *save;
	size_t i;

	dbg(ctx, "%s: base: \'%s\'\n", devname, cxldecoder_base);

	if (!path)
		return NULL;

	decoder = calloc(1, sizeof(*decoder));
	if (!decoder)
		goto err;

	decoder->id = id;
	decoder->ctx = ctx;
	decoder->port = port;
	list_head_init(&decoder->targets);
	list_head_init(&decoder->regions);
	list_head_init(&decoder->stale_regions);

	decoder->dev_path = strdup(cxldecoder_base);
	if (!decoder->dev_path)
		goto err_decoder;

	decoder->dev_buf = calloc(1, strlen(cxldecoder_base) + 50);
	if (!decoder->dev_buf)
		goto err_decoder;
	decoder->buf_len = strlen(cxldecoder_base) + 50;

	sprintf(path, "%s/start", cxldecoder_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		decoder->start = ULLONG_MAX;
	else
		decoder->start = strtoull(buf, NULL, 0);

	sprintf(path, "%s/size", cxldecoder_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		decoder->size = ULLONG_MAX;
	else
		decoder->size = strtoull(buf, NULL, 0);

	sprintf(path, "%s/mode", cxldecoder_base);
	if (sysfs_read_attr(ctx, path, buf) == 0) {
		if (strcmp(buf, "ram") == 0)
			decoder->mode = CXL_DECODER_MODE_RAM;
		else if (strcmp(buf, "pmem") == 0)
			decoder->mode = CXL_DECODER_MODE_PMEM;
		else if (strcmp(buf, "mixed") == 0)
			decoder->mode = CXL_DECODER_MODE_MIXED;
		else if (strcmp(buf, "none") == 0)
			decoder->mode = CXL_DECODER_MODE_NONE;
		else
			decoder->mode = CXL_DECODER_MODE_MIXED;
	} else
		decoder->mode = CXL_DECODER_MODE_NONE;

	sprintf(path, "%s/interleave_granularity", cxldecoder_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		decoder->interleave_granularity = UINT_MAX;
	else
		decoder->interleave_granularity = strtoul(buf, NULL, 0);

	sprintf(path, "%s/interleave_ways", cxldecoder_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		decoder->interleave_ways = UINT_MAX;
	else
		decoder->interleave_ways = strtoul(buf, NULL, 0);

	sprintf(path, "%s/qos_class", cxldecoder_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		decoder->qos_class = CXL_QOS_CLASS_NONE;
	else
		decoder->qos_class = atoi(buf);

	switch (port->type) {
	case CXL_PORT_ENDPOINT:
		sprintf(path, "%s/dpa_resource", cxldecoder_base);
		if (sysfs_read_attr(ctx, path, buf) < 0)
			decoder->dpa_resource = ULLONG_MAX;
		else
			decoder->dpa_resource = strtoull(buf, NULL, 0);
		sprintf(path, "%s/dpa_size", cxldecoder_base);
		if (sysfs_read_attr(ctx, path, buf) < 0)
			decoder->dpa_size = ULLONG_MAX;
		else
			decoder->dpa_size = strtoull(buf, NULL, 0);

	case CXL_PORT_SWITCH:
		decoder->pmem_capable = true;
		decoder->volatile_capable = true;
		decoder->mem_capable = true;
		decoder->accelmem_capable = true;
		sprintf(path, "%s/locked", cxldecoder_base);
		if (sysfs_read_attr(ctx, path, buf) == 0)
			decoder->locked = !!strtoul(buf, NULL, 0);
		sprintf(path, "%s/target_type", cxldecoder_base);
		if (sysfs_read_attr(ctx, path, buf) == 0) {
			if (strcmp(buf, "accelerator") == 0)
				decoder->target_type =
					CXL_DECODER_TTYPE_ACCELERATOR;
			if (strcmp(buf, "expander") == 0)
				decoder->target_type =
					CXL_DECODER_TTYPE_EXPANDER;
		}
		break;
	case CXL_PORT_ROOT: {
		struct cxl_decoder_flag {
			char *name;
			bool *flag;
		} flags[] = {
			{ "cap_type2", &decoder->accelmem_capable },
			{ "cap_type3", &decoder->mem_capable },
			{ "cap_ram", &decoder->volatile_capable },
			{ "cap_pmem", &decoder->pmem_capable },
			{ "locked", &decoder->locked },
		};

		for (i = 0; i < ARRAY_SIZE(flags); i++) {
			struct cxl_decoder_flag *flag = &flags[i];

			sprintf(path, "%s/%s", cxldecoder_base, flag->name);
			if (sysfs_read_attr(ctx, path, buf) == 0)
				*(flag->flag) = !!strtoul(buf, NULL, 0);
		}
		decoder->max_available_extent =
			cxl_decoder_calc_max_available_extent(decoder);
		break;
	}
	}

	sprintf(path, "%s/target_list", cxldecoder_base);
	if (sysfs_read_attr(ctx, path, buf) < 0)
		buf[0] = '\0';

	for (i = 0, target_id = strtok_r(buf, ",", &save); target_id;
	     target_id = strtok_r(NULL, ",", &save), i++) {
		int did = strtoul(target_id, NULL, 0);
		struct cxl_target *target = calloc(1, sizeof(*target));

		if (!target)
			break;

		target->id = did;
		target->position = i;
		target->decoder = decoder;
		sprintf(port->dev_buf, "%s/dport%d", port->dev_path, did);
		target->dev_path = realpath(port->dev_buf, NULL);
		if (!target->dev_path) {
			free(target);
			break;
		}
		sprintf(port->dev_buf, "%s/dport%d/physical_node", port->dev_path, did);
		target->phys_path = realpath(port->dev_buf, NULL);
		dbg(ctx, "%s: target%ld %s phys_path: %s\n", devname, i,
		    target->dev_path,
		    target->phys_path ? target->phys_path : "none");

		sprintf(port->dev_buf, "%s/dport%d/firmware_node", port->dev_path, did);
		target->fw_path = realpath(port->dev_buf, NULL);
		dbg(ctx, "%s: target%ld %s fw_path: %s\n", devname, i,
		    target->dev_path,
		    target->fw_path ? target->fw_path : "none");

		if (!target->phys_path && target->fw_path)
			target->phys_path = strdup(target->dev_path);
		list_add(&decoder->targets, &target->list);
	}

	if (target_id)
		err(ctx, "%s: failed to parse target%ld\n",
		    devpath_to_devname(cxldecoder_base), i);
	decoder->nr_targets = i;

	cxl_decoder_foreach(port, decoder_dup)
		if (decoder_dup->id == decoder->id) {
			free_decoder(decoder, NULL);
			free(path);
			return decoder_dup;
		}

	list_add_sorted(&port->decoders, decoder, list, decoder_id_cmp);

	free(path);
	return decoder;

err_decoder:
	free(decoder->dev_path);
	free(decoder->dev_buf);
	free(decoder);
err:
	free(path);
	return NULL;
}

static void cxl_decoders_init(struct cxl_port *port)
{
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);
	char *decoder_fmt;

	if (port->decoders_init)
		return;

	if (asprintf(&decoder_fmt, "decoder%d.", cxl_port_get_id(port)) < 0) {
		err(ctx, "%s: failed to add decoder(s)\n",
		    cxl_port_get_devname(port));
		return;
	}

	port->decoders_init = 1;

	device_parse(ctx, port->dev_path, decoder_fmt, port, add_cxl_decoder);

	free(decoder_fmt);
}

CXL_EXPORT struct cxl_decoder *cxl_decoder_get_first(struct cxl_port *port)
{
	cxl_decoders_init(port);

	return list_top(&port->decoders, struct cxl_decoder, list);
}

CXL_EXPORT struct cxl_decoder *cxl_decoder_get_next(struct cxl_decoder *decoder)
{
	struct cxl_port *port = decoder->port;

	return list_next(&port->decoders, decoder, list);
}

CXL_EXPORT struct cxl_decoder *cxl_decoder_get_last(struct cxl_port *port)
{
	cxl_decoders_init(port);

	return list_tail(&port->decoders, struct cxl_decoder, list);
}

CXL_EXPORT struct cxl_decoder *cxl_decoder_get_prev(struct cxl_decoder *decoder)
{
	struct cxl_port *port = decoder->port;

	return list_prev(&port->decoders, decoder, list);
}

CXL_EXPORT struct cxl_ctx *cxl_decoder_get_ctx(struct cxl_decoder *decoder)
{
	return decoder->ctx;
}

CXL_EXPORT int cxl_decoder_get_id(struct cxl_decoder *decoder)
{
	return decoder->id;
}

CXL_EXPORT struct cxl_port *cxl_decoder_get_port(struct cxl_decoder *decoder)
{
	return decoder->port;
}

CXL_EXPORT unsigned long long cxl_decoder_get_resource(struct cxl_decoder *decoder)
{
	return decoder->start;
}

CXL_EXPORT unsigned long long cxl_decoder_get_size(struct cxl_decoder *decoder)
{
	return decoder->size;
}

CXL_EXPORT int cxl_root_decoder_get_qos_class(struct cxl_decoder *decoder)
{
	if (!cxl_port_is_root(decoder->port))
		return -EINVAL;

	return decoder->qos_class;
}

CXL_EXPORT unsigned long long
cxl_decoder_get_dpa_resource(struct cxl_decoder *decoder)
{
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);

	if (!cxl_port_is_endpoint(port)) {
		err(ctx, "%s: not an endpoint decoder\n",
		    cxl_decoder_get_devname(decoder));
		return ULLONG_MAX;
	}

	return decoder->dpa_resource;
}

CXL_EXPORT unsigned long long
cxl_decoder_get_dpa_size(struct cxl_decoder *decoder)
{
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);

	if (!cxl_port_is_endpoint(port)) {
		err(ctx, "%s: not an endpoint decoder\n",
		    cxl_decoder_get_devname(decoder));
		return ULLONG_MAX;
	}

	return decoder->dpa_size;
}

CXL_EXPORT unsigned long long
cxl_decoder_get_max_available_extent(struct cxl_decoder *decoder)
{
	return decoder->max_available_extent;
}

CXL_EXPORT int cxl_decoder_set_dpa_size(struct cxl_decoder *decoder,
					unsigned long long size)
{
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);
	char *path = decoder->dev_buf;
	int len = decoder->buf_len, rc;
	char buf[SYSFS_ATTR_SIZE];

	if (!cxl_port_is_endpoint(port)) {
		err(ctx, "%s: not an endpoint decoder\n",
		    cxl_decoder_get_devname(decoder));
		return -EINVAL;
	}

	if (snprintf(path, len, "%s/dpa_size", decoder->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n",
		    cxl_decoder_get_devname(decoder));
		return -ENOMEM;
	}

	sprintf(buf, "%#llx\n", size);
	rc = sysfs_write_attr(ctx, path, buf);
	if (rc < 0)
		return rc;

	decoder->dpa_size = size;
	return 0;
}

CXL_EXPORT int cxl_decoder_set_mode(struct cxl_decoder *decoder,
				    enum cxl_decoder_mode mode)
{
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);
	char *path = decoder->dev_buf;
	int len = decoder->buf_len, rc;
	char buf[SYSFS_ATTR_SIZE];

	if (!cxl_port_is_endpoint(port)) {
		err(ctx, "%s: not an endpoint decoder\n",
		    cxl_decoder_get_devname(decoder));
		return -EINVAL;
	}

	switch (mode) {
	case CXL_DECODER_MODE_PMEM:
		sprintf(buf, "pmem");
		break;
	case CXL_DECODER_MODE_RAM:
		sprintf(buf, "ram");
		break;
	default:
		err(ctx, "%s: unsupported mode: %d\n",
		    cxl_decoder_get_devname(decoder), mode);
		return -EINVAL;
	}

	if (snprintf(path, len, "%s/mode", decoder->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n",
		    cxl_decoder_get_devname(decoder));
		return -ENOMEM;
	}

	rc = sysfs_write_attr(ctx, path, buf);
	if (rc < 0)
		return rc;

	decoder->mode = mode;
	return 0;
}

CXL_EXPORT enum cxl_decoder_mode
cxl_decoder_get_mode(struct cxl_decoder *decoder)
{
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);

	if (!cxl_port_is_endpoint(port)) {
		err(ctx, "%s: not an endpoint decoder\n",
		    cxl_decoder_get_devname(decoder));
		return CXL_DECODER_MODE_NONE;
	}

	return decoder->mode;
}

CXL_EXPORT enum cxl_decoder_target_type
cxl_decoder_get_target_type(struct cxl_decoder *decoder)
{
	return decoder->target_type;
}

CXL_EXPORT bool cxl_decoder_is_pmem_capable(struct cxl_decoder *decoder)
{
	return decoder->pmem_capable;
}

CXL_EXPORT bool cxl_decoder_is_volatile_capable(struct cxl_decoder *decoder)
{
	return decoder->volatile_capable;
}

CXL_EXPORT bool cxl_decoder_is_mem_capable(struct cxl_decoder *decoder)
{
	return decoder->mem_capable;
}

CXL_EXPORT bool cxl_decoder_is_accelmem_capable(struct cxl_decoder *decoder)
{
	return decoder->accelmem_capable;
}

CXL_EXPORT bool cxl_decoder_is_locked(struct cxl_decoder *decoder)
{
	return decoder->locked;
}

CXL_EXPORT unsigned int
cxl_decoder_get_interleave_granularity(struct cxl_decoder *decoder)
{
	return decoder->interleave_granularity;
}

CXL_EXPORT unsigned int
cxl_decoder_get_interleave_ways(struct cxl_decoder *decoder)
{
	return decoder->interleave_ways;
}

CXL_EXPORT struct cxl_region *
cxl_decoder_get_region(struct cxl_decoder *decoder)
{
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);
	char *path = decoder->dev_buf;
	char buf[SYSFS_ATTR_SIZE];
	struct cxl_region *region;
	struct cxl_decoder *iter;
	int rc;

	if (cxl_port_is_root(port))
		return NULL;

	sprintf(path, "%s/region", decoder->dev_path);
	rc = sysfs_read_attr(ctx, path, buf);
	if (rc < 0) {
		err(ctx, "failed to read region name: %s\n", strerror(-rc));
		return NULL;
	}

	if (strcmp(buf, "") == 0)
		return NULL;

	while (!cxl_port_is_root(port))
		port = cxl_port_get_parent(port);

	cxl_decoder_foreach(port, iter)
		cxl_region_foreach(iter, region)
			if (strcmp(cxl_region_get_devname(region), buf) == 0)
				return region;
	return NULL;
}

static struct cxl_region *cxl_decoder_create_region(struct cxl_decoder *decoder,
						    enum cxl_decoder_mode mode)
{
	struct cxl_ctx *ctx = cxl_decoder_get_ctx(decoder);
	char *path = decoder->dev_buf;
	char buf[SYSFS_ATTR_SIZE];
	struct cxl_region *region;
	int rc;

	if (mode == CXL_DECODER_MODE_PMEM)
		sprintf(path, "%s/create_pmem_region", decoder->dev_path);
	else if (mode == CXL_DECODER_MODE_RAM)
		sprintf(path, "%s/create_ram_region", decoder->dev_path);

	rc = sysfs_read_attr(ctx, path, buf);
	if (rc < 0) {
		err(ctx, "failed to read new region name: %s\n",
		    strerror(-rc));
		return NULL;
	}

	rc = sysfs_write_attr(ctx, path, buf);
	if (rc < 0) {
		err(ctx, "failed to write new region name: %s\n",
		    strerror(-rc));
		return NULL;
	}

	/* Force a re-init of regions so that the new one can be discovered */
	decoder->regions_init = 0;

	/* create_region was successful, walk to the new region */
	cxl_region_foreach(decoder, region) {
		const char *devname = cxl_region_get_devname(region);

		if (strcmp(devname, buf) == 0)
			goto found;
	}

	/*
	 * If walking to the region we just created failed, something has gone
	 * very wrong. Attempt to delete it to avoid leaving a dangling region
	 * id behind.
	 */
	err(ctx, "failed to add new region to libcxl\n");
	cxl_region_delete_name(decoder, buf);
	return NULL;

 found:
	return region;
}

CXL_EXPORT struct cxl_region *
cxl_decoder_create_pmem_region(struct cxl_decoder *decoder)
{
	return cxl_decoder_create_region(decoder, CXL_DECODER_MODE_PMEM);
}

CXL_EXPORT struct cxl_region *
cxl_decoder_create_ram_region(struct cxl_decoder *decoder)
{
	return cxl_decoder_create_region(decoder, CXL_DECODER_MODE_RAM);
}

CXL_EXPORT int cxl_decoder_get_nr_targets(struct cxl_decoder *decoder)
{
	return decoder->nr_targets;
}

CXL_EXPORT const char *cxl_decoder_get_devname(struct cxl_decoder *decoder)
{
	return devpath_to_devname(decoder->dev_path);
}

CXL_EXPORT struct cxl_memdev *
cxl_decoder_get_memdev(struct cxl_decoder *decoder)
{
	struct cxl_port *port = cxl_decoder_get_port(decoder);
	struct cxl_endpoint *ep;

	if (!port)
		return NULL;
	if (!cxl_port_is_endpoint(port))
		return NULL;

	ep = container_of(port, struct cxl_endpoint, port);
	if (!ep)
		return NULL;

	return cxl_endpoint_get_memdev(ep);
}

CXL_EXPORT struct cxl_target *cxl_target_get_first(struct cxl_decoder *decoder)
{
	return list_top(&decoder->targets, struct cxl_target, list);
}

CXL_EXPORT struct cxl_decoder *cxl_target_get_decoder(struct cxl_target *target)
{
	return target->decoder;
}

CXL_EXPORT struct cxl_target *cxl_target_get_next(struct cxl_target *target)
{
	struct cxl_decoder *decoder = cxl_target_get_decoder(target);

	return list_next(&decoder->targets, target, list);
}

CXL_EXPORT const char *cxl_target_get_devname(struct cxl_target *target)
{
	return devpath_to_devname(target->dev_path);
}

CXL_EXPORT unsigned long cxl_target_get_id(struct cxl_target *target)
{
	return target->id;
}

CXL_EXPORT int cxl_target_get_position(struct cxl_target *target)
{
	return target->position;
}

CXL_EXPORT bool cxl_target_maps_memdev(struct cxl_target *target,
					struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);

	dbg(ctx, "memdev: %s target: %s\n", memdev->host_path,
	    target->dev_path);

	if (target->phys_path)
		return !!strstr(memdev->host_path, target->phys_path);
	return !!strstr(memdev->host_path, target->dev_path);
}

CXL_EXPORT const char *cxl_target_get_physical_node(struct cxl_target *target)
{
	if (!target->phys_path)
		return NULL;
	return devpath_to_devname(target->phys_path);
}

CXL_EXPORT const char *cxl_target_get_firmware_node(struct cxl_target *target)
{
	if (!target->fw_path)
		return NULL;
	return devpath_to_devname(target->fw_path);
}

CXL_EXPORT struct cxl_target *
cxl_decoder_get_target_by_memdev(struct cxl_decoder *decoder,
				 struct cxl_memdev *memdev)
{
	struct cxl_target *target;

	cxl_target_foreach(decoder, target)
		if (cxl_target_maps_memdev(target, memdev))
			return target;
	return NULL;
}

CXL_EXPORT struct cxl_target *
cxl_decoder_get_target_by_position(struct cxl_decoder *decoder, int position)
{
	struct cxl_target *target;

	cxl_target_foreach(decoder, target)
		if (target->position == position)
			return target;
	return NULL;
}

static void *add_cxl_port(void *parent, int id, const char *cxlport_base)
{
	const char *devname = devpath_to_devname(cxlport_base);
	struct cxl_port *port, *port_dup;
	struct cxl_port *parent_port = parent;
	struct cxl_ctx *ctx = cxl_port_get_ctx(parent_port);
	int rc;

	dbg(ctx, "%s: base: \'%s\'\n", devname, cxlport_base);

	port = calloc(1, sizeof(*port));
	if (!port)
		return NULL;

	rc = cxl_port_init(port, parent_port, CXL_PORT_SWITCH, ctx, id,
			   cxlport_base);
	if (rc)
		goto err;

	cxl_port_foreach(parent_port, port_dup)
		if (port_dup->id == port->id) {
			free_port(port, NULL);
			return port_dup;
		}

	list_add(&parent_port->child_ports, &port->list);
	return port;

err:
	free(port);
	return NULL;

}

static void cxl_ports_init(struct cxl_port *port)
{
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);

	if (port->ports_init)
		return;

	port->ports_init = 1;

	device_parse(ctx, port->dev_path, "port", port, add_cxl_port);
}

CXL_EXPORT struct cxl_ctx *cxl_port_get_ctx(struct cxl_port *port)
{
	return port->ctx;
}

CXL_EXPORT struct cxl_port *cxl_port_get_first(struct cxl_port *port)
{
	cxl_ports_init(port);

	return list_top(&port->child_ports, struct cxl_port, list);
}

CXL_EXPORT struct cxl_port *cxl_port_get_next(struct cxl_port *port)
{
	struct cxl_port *parent_port = port->parent;

	return list_next(&parent_port->child_ports, port, list);
}

CXL_EXPORT struct cxl_port *cxl_port_get_next_all(struct cxl_port *port,
						  const struct cxl_port *top)
{
	struct cxl_port *child, *iter = port;

	child = cxl_port_get_first(iter);
	if (child)
		return child;
	while (!cxl_port_get_next(iter) && iter->parent && iter->parent != top)
		iter = iter->parent;
	return cxl_port_get_next(iter);
}

CXL_EXPORT const char *cxl_port_get_devname(struct cxl_port *port)
{
	return devpath_to_devname(port->dev_path);
}

CXL_EXPORT int cxl_port_get_id(struct cxl_port *port)
{
	return port->id;
}

CXL_EXPORT struct cxl_port *cxl_port_get_parent(struct cxl_port *port)
{
	return port->parent;
}

CXL_EXPORT bool cxl_port_is_root(struct cxl_port *port)
{
	return port->type == CXL_PORT_ROOT;
}

CXL_EXPORT bool cxl_port_is_switch(struct cxl_port *port)
{
	return port->type == CXL_PORT_SWITCH;
}

CXL_EXPORT bool cxl_port_is_endpoint(struct cxl_port *port)
{
	return port->type == CXL_PORT_ENDPOINT;
}

CXL_EXPORT int cxl_port_get_depth(struct cxl_port *port)
{
	return port->depth;
}

CXL_EXPORT struct cxl_bus *cxl_port_get_bus(struct cxl_port *port)
{
	struct cxl_bus *bus;

	if (!cxl_port_is_enabled(port))
		return NULL;

	if (port->bus)
		return port->bus;

	while (port->parent)
		port = port->parent;

	bus = container_of(port, typeof(*bus), port);
	port->bus = bus;
	return bus;
}

CXL_EXPORT const char *cxl_port_get_host(struct cxl_port *port)
{
	return devpath_to_devname(port->uport);
}

CXL_EXPORT struct cxl_dport *cxl_port_get_parent_dport(struct cxl_port *port)
{
	struct cxl_port *parent;
	struct cxl_dport *dport;
	const char *name;

	if (port->parent_dport)
		return port->parent_dport;

	if (!port->parent_dport_path)
		return NULL;

	parent = cxl_port_get_parent(port);
	name = devpath_to_devname(port->parent_dport_path);
	cxl_dport_foreach(parent, dport)
		if (strcmp(cxl_dport_get_devname(dport), name) == 0) {
			port->parent_dport = dport;
			return dport;
		}

	return NULL;
}

CXL_EXPORT bool cxl_port_hosts_memdev(struct cxl_port *port,
				      struct cxl_memdev *memdev)
{
	struct cxl_endpoint *endpoint = cxl_memdev_get_endpoint(memdev);
	struct cxl_port *iter;

	if (!endpoint)
		return false;

	iter = cxl_endpoint_get_port(endpoint);
	while (iter && iter != port)
		iter = iter->parent;
	return iter != NULL;
}

CXL_EXPORT int cxl_port_is_enabled(struct cxl_port *port)
{
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);
	char *path = port->dev_buf;
	int len = port->buf_len;

	if (snprintf(path, len, "%s/driver", port->dev_path) >= len) {
		err(ctx, "%s: buffer too small!\n", cxl_port_get_devname(port));
		return 0;
	}

	return is_enabled(path);
}

CXL_EXPORT int cxl_port_disable_invalidate(struct cxl_port *port)
{
	const char *devname = cxl_port_get_devname(port);
	struct cxl_bus *bus = cxl_port_get_bus(port);
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);

	if (cxl_port_is_root(port)) {
		err(ctx, "%s: can not be disabled through this interface\n",
		    devname);
		return -EINVAL;
	}

	if (!bus) {
		err(ctx, "%s: failed to invalidate\n", devname);
		return -ENXIO;
	}

	util_unbind(port->dev_path, ctx);

	if (cxl_port_is_enabled(port)) {
		err(ctx, "%s: failed to disable\n", devname);
		return -EBUSY;
	}

	dbg(ctx, "%s: disabled\n", devname);

	bus_invalidate(bus);

	return 0;
}

CXL_EXPORT int cxl_port_enable(struct cxl_port *port)
{
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);
	const char *devname = cxl_port_get_devname(port);

	if (cxl_port_is_enabled(port))
		return 0;

	util_bind(devname, port->module, "cxl", ctx);

	if (!cxl_port_is_enabled(port)) {
		err(ctx, "%s: failed to enable\n", devname);
		return -ENXIO;
	}

	dbg(ctx, "%s: enabled\n", devname);

	return 0;
}

CXL_EXPORT struct cxl_bus *cxl_port_to_bus(struct cxl_port *port)
{
	if (!cxl_port_is_root(port))
		return NULL;
	return container_of(port, struct cxl_bus, port);
}

CXL_EXPORT struct cxl_endpoint *cxl_port_to_endpoint(struct cxl_port *port)
{
	if (!cxl_port_is_endpoint(port))
		return NULL;
	return container_of(port, struct cxl_endpoint, port);
}

static void *add_cxl_dport(void *parent, int id, const char *cxldport_base)
{
	const char *devname = devpath_to_devname(cxldport_base);
	struct cxl_dport *dport, *dport_dup;
	struct cxl_port *port = parent;
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);

	dbg(ctx, "%s: base: \'%s\'\n", devname, cxldport_base);

	dport = calloc(1, sizeof(*dport));
	if (!dport)
		return NULL;

	dport->id = id;
	dport->port = port;

	dport->dev_path = realpath(cxldport_base, NULL);
	if (!dport->dev_path)
		goto err;

	dport->dev_buf = calloc(1, strlen(cxldport_base) + 50);
	if (!dport->dev_buf)
		goto err;
	dport->buf_len = strlen(cxldport_base) + 50;

	sprintf(dport->dev_buf, "%s/physical_node", cxldport_base);
	dport->phys_path = realpath(dport->dev_buf, NULL);

	sprintf(dport->dev_buf, "%s/firmware_node", cxldport_base);
	dport->fw_path = realpath(dport->dev_buf, NULL);

	if (!dport->phys_path && dport->fw_path)
		dport->phys_path = strdup(dport->dev_path);

	cxl_dport_foreach(port, dport_dup)
		if (dport_dup->id == dport->id) {
			free_dport(dport, NULL);
			return dport_dup;
		}

	port->nr_dports++;
	list_add(&port->dports, &dport->list);
	return dport;

err:
	free_dport(dport, NULL);
	return NULL;
}

static void cxl_dports_init(struct cxl_port *port)
{
	struct cxl_ctx *ctx = cxl_port_get_ctx(port);

	if (port->dports_init)
		return;

	port->dports_init = 1;

	device_parse(ctx, port->dev_path, "dport", port, add_cxl_dport);
}

CXL_EXPORT int cxl_port_get_nr_dports(struct cxl_port *port)
{
	if (!port->dports_init)
		cxl_dports_init(port);
	return port->nr_dports;
}

CXL_EXPORT struct cxl_dport *cxl_dport_get_first(struct cxl_port *port)
{
	cxl_dports_init(port);

	return list_top(&port->dports, struct cxl_dport, list);
}

CXL_EXPORT struct cxl_dport *cxl_dport_get_next(struct cxl_dport *dport)
{
	struct cxl_port *port = dport->port;

	return list_next(&port->dports, dport, list);
}

CXL_EXPORT const char *cxl_dport_get_devname(struct cxl_dport *dport)
{
	return devpath_to_devname(dport->dev_path);
}

CXL_EXPORT const char *cxl_dport_get_physical_node(struct cxl_dport *dport)
{
	if (!dport->phys_path)
		return NULL;
	return devpath_to_devname(dport->phys_path);
}

CXL_EXPORT const char *cxl_dport_get_firmware_node(struct cxl_dport *dport)
{
	if (!dport->fw_path)
		return NULL;
	return devpath_to_devname(dport->fw_path);
}

CXL_EXPORT int cxl_dport_get_id(struct cxl_dport *dport)
{
	return dport->id;
}

CXL_EXPORT struct cxl_port *cxl_dport_get_port(struct cxl_dport *dport)
{
	return dport->port;
}

CXL_EXPORT bool cxl_dport_maps_memdev(struct cxl_dport *dport,
				      struct cxl_memdev *memdev)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);

	dbg(ctx, "memdev: %s dport: %s\n", memdev->host_path, dport->dev_path);

	if (dport->phys_path)
		return !!strstr(memdev->host_path, dport->phys_path);
	return !!strstr(memdev->host_path, dport->dev_path);
}

CXL_EXPORT struct cxl_dport *
cxl_port_get_dport_by_memdev(struct cxl_port *port, struct cxl_memdev *memdev)
{
	struct cxl_dport *dport;

	cxl_dport_foreach(port, dport)
		if (cxl_dport_maps_memdev(dport, memdev))
			return dport;
	return NULL;
}

CXL_EXPORT int cxl_port_decoders_committed(struct cxl_port *port)
{
	return port->decoders_committed;
}

static void *add_cxl_bus(void *parent, int id, const char *cxlbus_base)
{
	const char *devname = devpath_to_devname(cxlbus_base);
	struct cxl_bus *bus, *bus_dup;
	struct cxl_ctx *ctx = parent;
	struct cxl_port *port;
	int rc;

	dbg(ctx, "%s: base: \'%s\'\n", devname, cxlbus_base);

	bus = calloc(1, sizeof(*bus));
	if (!bus)
		return NULL;

	port = &bus->port;
	rc = cxl_port_init(port, NULL, CXL_PORT_ROOT, ctx, id, cxlbus_base);
	if (rc)
		goto err;

	cxl_bus_foreach(ctx, bus_dup)
		if (bus_dup->port.id == bus->port.id) {
			free_bus(bus, NULL);
			return bus_dup;
		}

	list_add(&ctx->buses, &port->list);
	return bus;

err:
	free(bus);
	return NULL;
}

static void cxl_buses_init(struct cxl_ctx *ctx)
{
	if (ctx->buses_init)
		return;

	ctx->buses_init = 1;

	device_parse(ctx, "/sys/bus/cxl/devices", "root", ctx, add_cxl_bus);
}

CXL_EXPORT struct cxl_bus *cxl_bus_get_first(struct cxl_ctx *ctx)
{
	cxl_buses_init(ctx);

	return list_top(&ctx->buses, struct cxl_bus, port.list);
}

CXL_EXPORT struct cxl_bus *cxl_bus_get_next(struct cxl_bus *bus)
{
	struct cxl_ctx *ctx = bus->port.ctx;

	return list_next(&ctx->buses, bus, port.list);
}

CXL_EXPORT const char *cxl_bus_get_devname(struct cxl_bus *bus)
{
	struct cxl_port *port = &bus->port;

	return devpath_to_devname(port->dev_path);
}

CXL_EXPORT int cxl_bus_get_id(struct cxl_bus *bus)
{
	struct cxl_port *port = &bus->port;

	return port->id;
}

CXL_EXPORT struct cxl_port *cxl_bus_get_port(struct cxl_bus *bus)
{
	return &bus->port;
}

CXL_EXPORT const char *cxl_bus_get_provider(struct cxl_bus *bus)
{
	struct cxl_port *port = &bus->port;
	const char *devname = devpath_to_devname(port->uport);

	if (strcmp(devname, "ACPI0017:00") == 0)
		return "ACPI.CXL";
	if (strcmp(devname, "cxl_acpi.0") == 0)
		return "cxl_test";
	return devname;
}

CXL_EXPORT struct cxl_ctx *cxl_bus_get_ctx(struct cxl_bus *bus)
{
	return cxl_port_get_ctx(&bus->port);
}

CXL_EXPORT void cxl_cmd_unref(struct cxl_cmd *cmd)
{
	if (!cmd)
		return;
	if (--cmd->refcount == 0) {
		free(cmd->query_cmd);
		free(cmd->send_cmd);
		free(cmd->input_payload);
		free(cmd->output_payload);
		free(cmd);
	}
}

CXL_EXPORT void cxl_cmd_ref(struct cxl_cmd *cmd)
{
	cmd->refcount++;
}

static int cxl_cmd_alloc_query(struct cxl_cmd *cmd, int num_cmds)
{
	size_t size;

	if (!cmd)
		return -EINVAL;

	if (cmd->query_cmd != NULL)
		free(cmd->query_cmd);

	size = struct_size(cmd->query_cmd, commands, num_cmds);
	if (size == SIZE_MAX)
		return -EOVERFLOW;

	cmd->query_cmd = calloc(1, size);
	if (!cmd->query_cmd)
		return -ENOMEM;

	cmd->query_cmd->n_commands = num_cmds;

	return 0;
}

static struct cxl_cmd *cxl_cmd_new(struct cxl_memdev *memdev)
{
	struct cxl_cmd *cmd;
	size_t size;

	size = sizeof(*cmd);
	cmd = calloc(1, size);
	if (!cmd)
		return NULL;

	cxl_cmd_ref(cmd);
	cmd->memdev = memdev;

	return cmd;
}

static int __do_cmd(struct cxl_cmd *cmd, int ioctl_cmd, int fd)
{
	void *cmd_buf;
	int rc;

	switch (ioctl_cmd) {
	case CXL_MEM_QUERY_COMMANDS:
		cmd_buf = cmd->query_cmd;
		break;
	case CXL_MEM_SEND_COMMAND:
		cmd_buf = cmd->send_cmd;
		break;
	default:
		return -EINVAL;
	}

	rc = ioctl(fd, ioctl_cmd, cmd_buf);
	if (rc < 0)
		rc = -errno;

	return rc;
}

static int do_cmd(struct cxl_cmd *cmd, int ioctl_cmd)
{
	char *path;
	struct stat st;
	unsigned int major, minor;
	int rc = 0, fd;
	struct cxl_memdev *memdev = cmd->memdev;
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	const char *devname = cxl_memdev_get_devname(memdev);

	major = cxl_memdev_get_major(memdev);
	minor = cxl_memdev_get_minor(memdev);

	if (asprintf(&path, "/dev/cxl/%s", devname) < 0)
		return -ENOMEM;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		err(ctx, "failed to open %s: %s\n", path, strerror(errno));
		rc = -errno;
		goto out;
	}

	if (fstat(fd, &st) >= 0 && S_ISCHR(st.st_mode)
			&& major(st.st_rdev) == major
			&& minor(st.st_rdev) == minor) {
		rc = __do_cmd(cmd, ioctl_cmd, fd);
	} else {
		err(ctx, "failed to validate %s as a CXL memdev node\n", path);
		rc = -ENXIO;
	}
	close(fd);
out:
	free(path);
	return rc;
}

static int alloc_do_query(struct cxl_cmd *cmd, int num_cmds)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(cmd->memdev);
	int rc;

	rc = cxl_cmd_alloc_query(cmd, num_cmds);
	if (rc)
		return rc;

	rc = do_cmd(cmd, CXL_MEM_QUERY_COMMANDS);
	if (rc < 0)
		err(ctx, "%s: query commands failed: %s\n",
			cxl_memdev_get_devname(cmd->memdev),
			strerror(-rc));
	return rc;
}

static int cxl_cmd_do_query(struct cxl_cmd *cmd)
{
	struct cxl_memdev *memdev = cmd->memdev;
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	const char *devname = cxl_memdev_get_devname(memdev);
	int rc, n_commands;

	switch (cmd->query_status) {
	case CXL_CMD_QUERY_OK:
		return 0;
	case CXL_CMD_QUERY_UNSUPPORTED:
		return -EOPNOTSUPP;
	case CXL_CMD_QUERY_NOT_RUN:
		break;
	default:
		err(ctx, "%s: Unknown query_status %d\n",
			devname, cmd->query_status);
		return -EINVAL;
	}

	rc = alloc_do_query(cmd, 0);
	if (rc)
		return rc;

	n_commands = cmd->query_cmd->n_commands;
	dbg(ctx, "%s: supports %d commands\n", devname, n_commands);

	return alloc_do_query(cmd, n_commands);
}

static int cxl_cmd_validate(struct cxl_cmd *cmd, u32 cmd_id)
{
	struct cxl_memdev *memdev = cmd->memdev;
	struct cxl_mem_query_commands *query = cmd->query_cmd;
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	u32 i;

	for (i = 0; i < query->n_commands; i++) {
		struct cxl_command_info *cinfo = &query->commands[i];
		const char *cmd_name = cxl_command_names[cinfo->id].name;

		if (cinfo->id != cmd_id)
			continue;

		dbg(ctx, "%s: %s: in: %d, out %d, flags: %#08x\n",
			devname, cmd_name, cinfo->size_in,
			cinfo->size_out, cinfo->flags);

		cmd->query_idx = i;
		cmd->query_status = CXL_CMD_QUERY_OK;
		return 0;
	}
	cmd->query_status = CXL_CMD_QUERY_UNSUPPORTED;
	return -EOPNOTSUPP;
}

CXL_EXPORT int cxl_cmd_set_input_payload(struct cxl_cmd *cmd, void *buf,
		int size)
{
	struct cxl_memdev *memdev = cmd->memdev;

	if (size > memdev->payload_max || size < 0)
		return -EINVAL;

	if (!buf) {

		/* If the user didn't supply a buffer, allocate it */
		cmd->input_payload = calloc(1, size);
		if (!cmd->input_payload)
			return -ENOMEM;
		cmd->send_cmd->in.payload = (u64)cmd->input_payload;
	} else {
		/*
		 * Use user-buffer as is. If an automatic allocation was
		 * previously made (based on a fixed size from query),
		 * it will get freed during unref.
		 */
		cmd->send_cmd->in.payload = (u64)buf;
	}
	cmd->send_cmd->in.size = size;

	return 0;
}

CXL_EXPORT int cxl_cmd_set_output_payload(struct cxl_cmd *cmd, void *buf,
		int size)
{
	struct cxl_memdev *memdev = cmd->memdev;

	if (size > memdev->payload_max || size < 0)
		return -EINVAL;

	if (!buf) {

		/* If the user didn't supply a buffer, allocate it */
		cmd->output_payload = calloc(1, size);
		if (!cmd->output_payload)
			return -ENOMEM;
		cmd->send_cmd->out.payload = (u64)cmd->output_payload;
	} else {
		/*
		 * Use user-buffer as is. If an automatic allocation was
		 * previously made (based on a fixed size from query),
		 * it will get freed during unref.
		 */
		cmd->send_cmd->out.payload = (u64)buf;
	}
	cmd->send_cmd->out.size = size;

	return 0;
}

static int cxl_cmd_alloc_send(struct cxl_cmd *cmd, u32 cmd_id)
{
	struct cxl_mem_query_commands *query = cmd->query_cmd;
	struct cxl_command_info *cinfo = &query->commands[cmd->query_idx];
	size_t size;

	size = sizeof(struct cxl_send_command);
	cmd->send_cmd = calloc(1, size);
	if (!cmd->send_cmd)
		return -ENOMEM;

	if (cinfo->id != cmd_id)
		return -EINVAL;

	cmd->send_cmd->id = cmd_id;

	if (cinfo->size_in > 0) {
		cmd->input_payload = calloc(1, cinfo->size_in);
		if (!cmd->input_payload)
			return -ENOMEM;
		cmd->send_cmd->in.payload = (u64)cmd->input_payload;
		cmd->send_cmd->in.size = cinfo->size_in;
	}
	if (cinfo->size_out > 0) {
		cmd->output_payload = calloc(1, cinfo->size_out);
		if (!cmd->output_payload)
			return -ENOMEM;
		cmd->send_cmd->out.payload = (u64)cmd->output_payload;
		cmd->send_cmd->out.size = cinfo->size_out;
	}

	return 0;
}

static struct cxl_cmd *cxl_cmd_new_generic(struct cxl_memdev *memdev,
		u32 cmd_id)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc;

	cmd = cxl_cmd_new(memdev);
	if (!cmd)
		return NULL;

	rc = cxl_cmd_do_query(cmd);
	if (rc) {
		err(ctx, "%s: query returned: %s\n", devname, strerror(-rc));
		goto fail;
	}

	rc = cxl_cmd_validate(cmd, cmd_id);
	if (rc) {
		errno = -rc;
		goto fail;
	}

	rc = cxl_cmd_alloc_send(cmd, cmd_id);
	if (rc) {
		errno = -rc;
		goto fail;
	}

	cmd->status = 1;
	return cmd;

fail:
	cxl_cmd_unref(cmd);
	return NULL;
}

CXL_EXPORT const char *cxl_cmd_get_devname(struct cxl_cmd *cmd)
{
	return cxl_memdev_get_devname(cmd->memdev);
}

static int cxl_cmd_validate_status(struct cxl_cmd *cmd, u32 id)
{
	if (cmd->send_cmd->id != id)
		return -EINVAL;
	if (cmd->status < 0)
		return cmd->status;
	return 0;
}

static uint64_t cxl_capacity_to_bytes(leint64_t size)
{
	return le64_to_cpu(size) * CXL_CAPACITY_MULTIPLIER;
}

/* Helpers for health_info fields (no endian conversion) */
#define cmd_get_field_u8(cmd, n, N, field)				\
do {									\
	struct cxl_cmd_##n *c =						\
		(struct cxl_cmd_##n *)cmd->send_cmd->out.payload;	\
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_##N);	\
	if (rc)								\
		return rc;						\
	return c->field;						\
} while(0)

#define cmd_get_field_u16(cmd, n, N, field)				\
do {									\
	struct cxl_cmd_##n *c =						\
		(struct cxl_cmd_##n *)cmd->send_cmd->out.payload;	\
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_##N);	\
	if (rc)								\
		return rc;						\
	return le16_to_cpu(c->field);					\
} while(0)


#define cmd_get_field_u32(cmd, n, N, field)				\
do {									\
	struct cxl_cmd_##n *c =						\
		(struct cxl_cmd_##n *)cmd->send_cmd->out.payload;	\
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_##N);	\
	if (rc)								\
		return rc;						\
	return le32_to_cpu(c->field);					\
} while(0)


#define cmd_get_field_u8_mask(cmd, n, N, field, mask)			\
do {									\
	struct cxl_cmd_##n *c =						\
		(struct cxl_cmd_##n *)cmd->send_cmd->out.payload;	\
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_##N);	\
	if (rc)								\
		return rc;						\
	return !!(c->field & mask);					\
} while(0)

CXL_EXPORT struct cxl_cmd *
cxl_cmd_new_get_alert_config(struct cxl_memdev *memdev)
{
	return cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_GET_ALERT_CONFIG);
}

#define cmd_alert_get_valid_alerts_field(c, m)                                 \
	cmd_get_field_u8_mask(c, get_alert_config, GET_ALERT_CONFIG,           \
			      valid_alerts, m)

CXL_EXPORT int
cxl_cmd_alert_config_life_used_prog_warn_threshold_valid(struct cxl_cmd *cmd)
{
	cmd_alert_get_valid_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_VALID_ALERTS_LIFE_USED_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_dev_over_temperature_prog_warn_threshold_valid(
	struct cxl_cmd *cmd)
{
	cmd_alert_get_valid_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_VALID_ALERTS_DEV_OVER_TEMPERATURE_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_dev_under_temperature_prog_warn_threshold_valid(
	struct cxl_cmd *cmd)
{
	cmd_alert_get_valid_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_VALID_ALERTS_DEV_UNDER_TEMPERATURE_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_corrected_volatile_mem_err_prog_warn_threshold_valid(
	struct cxl_cmd *cmd)
{
	cmd_alert_get_valid_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_VALID_ALERTS_CORRECTED_VOLATILE_MEM_ERR_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_corrected_pmem_err_prog_warn_threshold_valid(
	struct cxl_cmd *cmd)
{
	cmd_alert_get_valid_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_VALID_ALERTS_CORRECTED_PMEM_ERR_PROG_WARN_THRESHOLD_MASK);
}

#define cmd_alert_get_prog_alerts_field(c, m)                                  \
	cmd_get_field_u8_mask(c, get_alert_config, GET_ALERT_CONFIG,           \
			      programmable_alerts, m)

CXL_EXPORT int
cxl_cmd_alert_config_life_used_prog_warn_threshold_writable(struct cxl_cmd *cmd)
{
	cmd_alert_get_prog_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_PROG_ALERTS_LIFE_USED_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_dev_over_temperature_prog_warn_threshold_writable(
	struct cxl_cmd *cmd)
{
	cmd_alert_get_prog_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_PROG_ALERTS_DEV_OVER_TEMPERATURE_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_dev_under_temperature_prog_warn_threshold_writable(
	struct cxl_cmd *cmd)
{
	cmd_alert_get_prog_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_PROG_ALERTS_DEV_UNDER_TEMPERATURE_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_corrected_volatile_mem_err_prog_warn_threshold_writable(
	struct cxl_cmd *cmd)
{
	cmd_alert_get_prog_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_PROG_ALERTS_CORRECTED_VOLATILE_MEM_ERR_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_corrected_pmem_err_prog_warn_threshold_writable(
	struct cxl_cmd *cmd)
{
	cmd_alert_get_prog_alerts_field(
		cmd,
		CXL_CMD_ALERT_CONFIG_PROG_ALERTS_CORRECTED_PMEM_ERR_PROG_WARN_THRESHOLD_MASK);
}

CXL_EXPORT int
cxl_cmd_alert_config_get_life_used_crit_alert_threshold(struct cxl_cmd *cmd)
{
	cmd_get_field_u8(cmd, get_alert_config, GET_ALERT_CONFIG,
			 life_used_crit_alert_threshold);
}

CXL_EXPORT int
cxl_cmd_alert_config_get_life_used_prog_warn_threshold(struct cxl_cmd *cmd)
{
	cmd_get_field_u8(cmd, get_alert_config, GET_ALERT_CONFIG,
			 life_used_prog_warn_threshold);
}

CXL_EXPORT int
cxl_cmd_alert_config_get_dev_over_temperature_crit_alert_threshold(
	struct cxl_cmd *cmd)
{
	cmd_get_field_u16(cmd, get_alert_config, GET_ALERT_CONFIG,
			  dev_over_temperature_crit_alert_threshold);
}

CXL_EXPORT int
cxl_cmd_alert_config_get_dev_under_temperature_crit_alert_threshold(
	struct cxl_cmd *cmd)
{
	cmd_get_field_u16(cmd, get_alert_config, GET_ALERT_CONFIG,
			  dev_under_temperature_crit_alert_threshold);
}

CXL_EXPORT int
cxl_cmd_alert_config_get_dev_over_temperature_prog_warn_threshold(
	struct cxl_cmd *cmd)
{
	cmd_get_field_u16(cmd, get_alert_config, GET_ALERT_CONFIG,
			  dev_over_temperature_prog_warn_threshold);
}

CXL_EXPORT int
cxl_cmd_alert_config_get_dev_under_temperature_prog_warn_threshold(
	struct cxl_cmd *cmd)
{
	cmd_get_field_u16(cmd, get_alert_config, GET_ALERT_CONFIG,
			  dev_under_temperature_prog_warn_threshold);
}

CXL_EXPORT int
cxl_cmd_alert_config_get_corrected_volatile_mem_err_prog_warn_threshold(
	struct cxl_cmd *cmd)
{
	cmd_get_field_u16(cmd, get_alert_config, GET_ALERT_CONFIG,
			  corrected_volatile_mem_err_prog_warn_threshold);
}

CXL_EXPORT int cxl_cmd_alert_config_get_corrected_pmem_err_prog_warn_threshold(
	struct cxl_cmd *cmd)
{
	cmd_get_field_u16(cmd, get_alert_config, GET_ALERT_CONFIG,
			  corrected_pmem_err_prog_warn_threshold);
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_get_health_info(
		struct cxl_memdev *memdev)
{
	return cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_GET_HEALTH_INFO);
}

#define cmd_health_get_status_field(c, m)					\
	cmd_get_field_u8_mask(c, get_health_info, GET_HEALTH_INFO, health_status, m)

CXL_EXPORT int cxl_cmd_health_info_get_maintenance_needed(struct cxl_cmd *cmd)
{
	cmd_health_get_status_field(cmd,
		CXL_CMD_HEALTH_INFO_STATUS_MAINTENANCE_NEEDED_MASK);
}

CXL_EXPORT int cxl_cmd_health_info_get_performance_degraded(struct cxl_cmd *cmd)
{
	cmd_health_get_status_field(cmd,
		CXL_CMD_HEALTH_INFO_STATUS_PERFORMANCE_DEGRADED_MASK);
}

CXL_EXPORT int cxl_cmd_health_info_get_hw_replacement_needed(struct cxl_cmd *cmd)
{
	cmd_health_get_status_field(cmd,
		CXL_CMD_HEALTH_INFO_STATUS_HW_REPLACEMENT_NEEDED_MASK);
}

#define cmd_health_check_media_field(cmd, f)					\
do {										\
	struct cxl_cmd_get_health_info *c =					\
		(struct cxl_cmd_get_health_info *)cmd->send_cmd->out.payload;	\
	int rc = cxl_cmd_validate_status(cmd,					\
			CXL_MEM_COMMAND_ID_GET_HEALTH_INFO);			\
	if (rc)									\
		return rc;							\
	return (c->media_status == f);						\
} while(0)

CXL_EXPORT int cxl_cmd_health_info_get_media_normal(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_NORMAL);
}

CXL_EXPORT int cxl_cmd_health_info_get_media_not_ready(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_NOT_READY);
}

CXL_EXPORT int
cxl_cmd_health_info_get_media_persistence_lost(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_PERSISTENCE_LOST);
}

CXL_EXPORT int cxl_cmd_health_info_get_media_data_lost(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_DATA_LOST);
}

CXL_EXPORT int
cxl_cmd_health_info_get_media_powerloss_persistence_loss(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_POWERLOSS_PERSISTENCE_LOSS);
}

CXL_EXPORT int
cxl_cmd_health_info_get_media_shutdown_persistence_loss(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_SHUTDOWN_PERSISTENCE_LOSS);
}

CXL_EXPORT int
cxl_cmd_health_info_get_media_persistence_loss_imminent(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_PERSISTENCE_LOSS_IMMINENT);
}

CXL_EXPORT int
cxl_cmd_health_info_get_media_powerloss_data_loss(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_POWERLOSS_DATA_LOSS);
}

CXL_EXPORT int
cxl_cmd_health_info_get_media_shutdown_data_loss(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_SHUTDOWN_DATA_LOSS);
}

CXL_EXPORT int
cxl_cmd_health_info_get_media_data_loss_imminent(struct cxl_cmd *cmd)
{
	cmd_health_check_media_field(cmd,
		CXL_CMD_HEALTH_INFO_MEDIA_STATUS_DATA_LOSS_IMMINENT);
}

#define cmd_health_check_ext_field(cmd, fname, type)				\
do {										\
	struct cxl_cmd_get_health_info *c =					\
		(struct cxl_cmd_get_health_info *)cmd->send_cmd->out.payload;	\
	int rc = cxl_cmd_validate_status(cmd,					\
			CXL_MEM_COMMAND_ID_GET_HEALTH_INFO);			\
	if (rc)									\
		return rc;							\
	return (FIELD_GET(fname##_MASK, c->ext_status) ==			\
		fname##_##type);						\
} while(0)

CXL_EXPORT int
cxl_cmd_health_info_get_ext_life_used_normal(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_LIFE_USED, NORMAL);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_life_used_warning(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_LIFE_USED, WARNING);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_life_used_critical(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_LIFE_USED, CRITICAL);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_temperature_normal(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_TEMPERATURE, NORMAL);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_temperature_warning(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_TEMPERATURE, WARNING);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_temperature_critical(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_TEMPERATURE, CRITICAL);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_corrected_volatile_normal(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_CORRECTED_VOLATILE, NORMAL);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_corrected_volatile_warning(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_CORRECTED_VOLATILE, WARNING);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_corrected_persistent_normal(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_CORRECTED_PERSISTENT, NORMAL);
}

CXL_EXPORT int
cxl_cmd_health_info_get_ext_corrected_persistent_warning(struct cxl_cmd *cmd)
{
	cmd_health_check_ext_field(cmd,
		CXL_CMD_HEALTH_INFO_EXT_CORRECTED_PERSISTENT, WARNING);
}

static int health_info_get_life_used_raw(struct cxl_cmd *cmd)
{
	cmd_get_field_u8(cmd, get_health_info, GET_HEALTH_INFO,
				life_used);
}

CXL_EXPORT int cxl_cmd_health_info_get_life_used(struct cxl_cmd *cmd)
{
	int rc = health_info_get_life_used_raw(cmd);

	if (rc < 0)
		return rc;
	if (rc == CXL_CMD_HEALTH_INFO_LIFE_USED_NOT_IMPL)
		return -EOPNOTSUPP;
	return rc;
}

static int health_info_get_temperature_raw(struct cxl_cmd *cmd)
{
	cmd_get_field_u16(cmd, get_health_info, GET_HEALTH_INFO,
				 temperature);
}

CXL_EXPORT int cxl_cmd_health_info_get_temperature(struct cxl_cmd *cmd)
{
	int rc = health_info_get_temperature_raw(cmd);

	if (rc < 0)
		return rc;
	if (rc == CXL_CMD_HEALTH_INFO_TEMPERATURE_NOT_IMPL)
		return -EOPNOTSUPP;
	return rc;
}

CXL_EXPORT int cxl_cmd_health_info_get_dirty_shutdowns(struct cxl_cmd *cmd)
{
	cmd_get_field_u32(cmd, get_health_info, GET_HEALTH_INFO,
				 dirty_shutdowns);
}

CXL_EXPORT int cxl_cmd_health_info_get_volatile_errors(struct cxl_cmd *cmd)
{
	cmd_get_field_u32(cmd, get_health_info, GET_HEALTH_INFO,
				 volatile_errors);
}

CXL_EXPORT int cxl_cmd_health_info_get_pmem_errors(struct cxl_cmd *cmd)
{
	cmd_get_field_u32(cmd, get_health_info, GET_HEALTH_INFO,
				 pmem_errors);
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_identify(struct cxl_memdev *memdev)
{
	return cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_IDENTIFY);
}

static struct cxl_cmd_identify *
cmd_to_identify(struct cxl_cmd *cmd)
{
	if (cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_IDENTIFY))
		return NULL;

	return cmd->output_payload;
}

CXL_EXPORT int cxl_cmd_identify_get_fw_rev(struct cxl_cmd *cmd, char *fw_rev,
		int fw_len)
{
	struct cxl_cmd_identify *id =
			(struct cxl_cmd_identify *)cmd->send_cmd->out.payload;

	if (cmd->send_cmd->id != CXL_MEM_COMMAND_ID_IDENTIFY)
		return -EINVAL;
	if (cmd->status < 0)
		return cmd->status;

	if (fw_len > 0)
		memcpy(fw_rev, id->fw_revision,
			min(fw_len, CXL_CMD_IDENTIFY_FW_REV_LENGTH));
	return 0;
}

CXL_EXPORT unsigned long long cxl_cmd_identify_get_partition_align(
		struct cxl_cmd *cmd)
{
	struct cxl_cmd_identify *c;

	c = cmd_to_identify(cmd);
	if (!c)
		return ULLONG_MAX;
	return cxl_capacity_to_bytes(c->partition_align);
}

CXL_EXPORT unsigned int cxl_cmd_identify_get_label_size(struct cxl_cmd *cmd)
{
	struct cxl_cmd_identify *id =
			(struct cxl_cmd_identify *)cmd->send_cmd->out.payload;

	if (cmd->send_cmd->id != CXL_MEM_COMMAND_ID_IDENTIFY)
		return -EINVAL;
	if (cmd->status < 0)
		return cmd->status;

	return le32_to_cpu(id->lsa_size);
}

CXL_EXPORT unsigned long long
cxl_cmd_identify_get_total_size(struct cxl_cmd *cmd)
{
	struct cxl_cmd_identify *c;

	c = cmd_to_identify(cmd);
	if (!c)
		return ULLONG_MAX;
	return cxl_capacity_to_bytes(c->total_capacity);
}

CXL_EXPORT unsigned long long
cxl_cmd_identify_get_volatile_only_size(struct cxl_cmd *cmd)
{
	struct cxl_cmd_identify *c;

	c = cmd_to_identify(cmd);
	if (!c)
		return ULLONG_MAX;
	return cxl_capacity_to_bytes(c->volatile_capacity);
}

CXL_EXPORT unsigned long long
cxl_cmd_identify_get_persistent_only_size(struct cxl_cmd *cmd)
{
	struct cxl_cmd_identify *c;

	c = cmd_to_identify(cmd);
	if (!c)
		return ULLONG_MAX;
	return cxl_capacity_to_bytes(c->persistent_capacity);
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_raw(struct cxl_memdev *memdev,
		int opcode)
{
	struct cxl_cmd *cmd;

	/* opcode '0' is reserved */
	if (opcode <= 0) {
		errno = EINVAL;
		return NULL;
	}

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_RAW);
	if (!cmd)
		return NULL;

	cmd->send_cmd->raw.opcode = opcode;
	return cmd;
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_read_label(struct cxl_memdev *memdev,
		unsigned int offset, unsigned int length)
{
	struct cxl_cmd_get_lsa_in *get_lsa;
	struct cxl_cmd *cmd;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_GET_LSA);
	if (!cmd)
		return NULL;

	get_lsa = (struct cxl_cmd_get_lsa_in *)cmd->send_cmd->in.payload;
	get_lsa->offset = cpu_to_le32(offset);
	get_lsa->length = cpu_to_le32(length);
	return cmd;
}

CXL_EXPORT ssize_t cxl_cmd_read_label_get_payload(struct cxl_cmd *cmd,
		void *buf, unsigned int length)
{
	struct cxl_cmd_get_lsa_in *get_lsa;
	void *payload;
	int rc;

	rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_LSA);
	if (rc)
		return rc;

	get_lsa = (struct cxl_cmd_get_lsa_in *)cmd->send_cmd->in.payload;
	if (length > le32_to_cpu(get_lsa->length))
		return -EINVAL;

	payload = (void *)cmd->send_cmd->out.payload;
	memcpy(buf, payload, length);
	return length;
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_get_partition(struct cxl_memdev *memdev)
{
	return cxl_cmd_new_generic(memdev,
				   CXL_MEM_COMMAND_ID_GET_PARTITION_INFO);
}

static struct cxl_cmd_get_partition *
cmd_to_get_partition(struct cxl_cmd *cmd)
{
	if (cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_PARTITION_INFO))
		return NULL;

	return cmd->output_payload;
}

CXL_EXPORT unsigned long long
cxl_cmd_partition_get_active_volatile_size(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_partition *c;

	c = cmd_to_get_partition(cmd);
	if (!c)
		return ULLONG_MAX;
	return cxl_capacity_to_bytes(c->active_volatile);
}

CXL_EXPORT unsigned long long
cxl_cmd_partition_get_active_persistent_size(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_partition *c;

	c = cmd_to_get_partition(cmd);
	if (!c)
		return ULLONG_MAX;
	return cxl_capacity_to_bytes(c->active_persistent);
}

CXL_EXPORT unsigned long long
cxl_cmd_partition_get_next_volatile_size(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_partition *c;

	c = cmd_to_get_partition(cmd);
	if (!c)
		return ULLONG_MAX;
	return cxl_capacity_to_bytes(c->next_volatile);
}

CXL_EXPORT unsigned long long
cxl_cmd_partition_get_next_persistent_size(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_partition *c;

	c = cmd_to_get_partition(cmd);
	if (!c)
		return ULLONG_MAX;
	return cxl_capacity_to_bytes(c->next_persistent);
}

CXL_EXPORT int cxl_cmd_partition_set_mode(struct cxl_cmd *cmd,
		enum cxl_setpartition_mode mode)
{
	struct cxl_cmd_set_partition *setpart = cmd->input_payload;

	if (mode == CXL_SETPART_IMMEDIATE)
		setpart->flags = CXL_CMD_SET_PARTITION_FLAG_IMMEDIATE;
	else
		setpart->flags = !CXL_CMD_SET_PARTITION_FLAG_IMMEDIATE;

	return 0;
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_set_partition(struct cxl_memdev *memdev,
		unsigned long long volatile_size)
{
	struct cxl_cmd_set_partition *setpart;
	struct cxl_cmd *cmd;

	cmd = cxl_cmd_new_generic(memdev,
			CXL_MEM_COMMAND_ID_SET_PARTITION_INFO);

	setpart = cmd->input_payload;
	setpart->volatile_size = cpu_to_le64(volatile_size)
					/ CXL_CAPACITY_MULTIPLIER;
	return cmd;
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_get_fw_info(struct cxl_memdev *memdev)
{
	return cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_GET_FW_INFO);
}

static struct cxl_cmd_get_fw_info *cmd_to_get_fw_info(struct cxl_cmd *cmd)
{
	if (cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_FW_INFO))
		return NULL;

	return cmd->output_payload;
}

CXL_EXPORT unsigned int cxl_cmd_fw_info_get_num_slots(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_fw_info *c = cmd_to_get_fw_info(cmd);

	if (!c)
		return 0;

	return c->num_slots;
}

CXL_EXPORT unsigned int cxl_cmd_fw_info_get_active_slot(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_fw_info *c = cmd_to_get_fw_info(cmd);

	if (!c)
		return 0;

	return c->slot_info & CXL_FW_INFO_CUR_SLOT_MASK;
}

CXL_EXPORT unsigned int cxl_cmd_fw_info_get_staged_slot(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_fw_info *c = cmd_to_get_fw_info(cmd);

	if (!c)
		return 0;

	return (c->slot_info & CXL_FW_INFO_NEXT_SLOT_MASK) >>
	       CXL_FW_INFO_NEXT_SLOT_SHIFT;
}

CXL_EXPORT bool cxl_cmd_fw_info_get_online_activate_capable(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_fw_info *c = cmd_to_get_fw_info(cmd);

	if (!c)
		return false;

	return !!(c->activation_cap & CXL_FW_INFO_HAS_LIVE_ACTIVATE);
}

CXL_EXPORT int cxl_cmd_fw_info_get_fw_ver(struct cxl_cmd *cmd, int slot,
					  char *buf, unsigned int len)
{
	struct cxl_cmd_get_fw_info *c = cmd_to_get_fw_info(cmd);
	char *fw_ver;

	if (!c)
		return -ENXIO;
	if (!len)
		return -EINVAL;

	switch(slot) {
	case 1:
		fw_ver = c->slot_1_revision;
		break;
	case 2:
		fw_ver = c->slot_2_revision;
		break;
	case 3:
		fw_ver = c->slot_3_revision;
		break;
	case 4:
		fw_ver = c->slot_4_revision;
		break;
	default:
		return -EINVAL;
	}

	if (fw_ver[0] == 0)
		return -ENOENT;

	memcpy(buf, fw_ver, min(len, (unsigned int)CXL_FW_VERSION_STR_LEN));

	return 0;
}

CXL_EXPORT int cxl_cmd_submit(struct cxl_cmd *cmd)
{
	struct cxl_memdev *memdev = cmd->memdev;
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	int rc;

	switch (cmd->query_status) {
	case CXL_CMD_QUERY_OK:
		break;
	case CXL_CMD_QUERY_UNSUPPORTED:
		return -EOPNOTSUPP;
	case CXL_CMD_QUERY_NOT_RUN:
		return -EINVAL;
	default:
		err(ctx, "%s: Unknown query_status %d\n",
			devname, cmd->query_status);
		return -EINVAL;
	}

	dbg(ctx, "%s: submitting SEND cmd: in: %d, out: %d\n", devname,
		cmd->send_cmd->in.size, cmd->send_cmd->out.size);
	rc = do_cmd(cmd, CXL_MEM_SEND_COMMAND);
	cmd->status = cmd->send_cmd->retval;
	dbg(ctx, "%s: got SEND cmd: in: %d, out: %d, retval: %d, status: %d\n",
		devname, cmd->send_cmd->in.size, cmd->send_cmd->out.size,
		rc, cmd->status);

	return rc;
}

CXL_EXPORT int cxl_cmd_get_mbox_status(struct cxl_cmd *cmd)
{
	return cmd->status;
}

CXL_EXPORT int cxl_cmd_get_out_size(struct cxl_cmd *cmd)
{
	return cmd->send_cmd->out.size;
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_write_label(struct cxl_memdev *memdev,
		void *lsa_buf, unsigned int offset, unsigned int length)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd_set_lsa *set_lsa;
	struct cxl_cmd *cmd;
	int rc;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_SET_LSA);
	if (!cmd)
		return NULL;

	/* this will allocate 'in.payload' */
	rc = cxl_cmd_set_input_payload(cmd, NULL, sizeof(*set_lsa) + length);
	if (rc) {
		err(ctx, "%s: cmd setup failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
		goto out_fail;
	}
	set_lsa = (struct cxl_cmd_set_lsa *)cmd->send_cmd->in.payload;
	set_lsa->offset = cpu_to_le32(offset);
	memcpy(set_lsa->lsa_data, lsa_buf, length);

	return cmd;

out_fail:
	cxl_cmd_unref(cmd);
	return NULL;
}

enum lsa_op {
	LSA_OP_GET,
	LSA_OP_SET,
	LSA_OP_ZERO,
};

static int __lsa_op(struct cxl_memdev *memdev, int op, void *buf,
		size_t length, size_t offset)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	void *zero_buf = NULL;
	struct cxl_cmd *cmd;
	ssize_t ret_len;
	int rc = 0;

	switch (op) {
	case LSA_OP_GET:
		cmd = cxl_cmd_new_read_label(memdev, offset, length);
		if (!cmd)
			return -ENOMEM;
		rc = cxl_cmd_set_output_payload(cmd, buf, length);
		if (rc) {
			err(ctx, "%s: cmd setup failed: %s\n",
			    cxl_memdev_get_devname(memdev), strerror(-rc));
			goto out;
		}
		break;
	case LSA_OP_ZERO:
		zero_buf = calloc(1, length);
		if (!zero_buf)
			return -ENOMEM;
		buf = zero_buf;
		/* fall through */
	case LSA_OP_SET:
		cmd = cxl_cmd_new_write_label(memdev, buf, offset, length);
		if (!cmd) {
			rc = -ENOMEM;
			goto out_free;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n",
			devname, strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n",
			devname, rc);
		rc = -ENXIO;
		goto out;
	}

	if (op == LSA_OP_GET) {
		ret_len = cxl_cmd_read_label_get_payload(cmd, buf, length);
		if (ret_len < 0) {
			rc = ret_len;
			goto out;
		}
	}

out:
	cxl_cmd_unref(cmd);
out_free:
	free(zero_buf);
	return rc;

}

static int lsa_op(struct cxl_memdev *memdev, int op, void *buf,
		size_t length, size_t offset)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	size_t remaining = length, cur_len, cur_off = 0;
	int label_iter_max, rc = 0;

	if (op != LSA_OP_ZERO && buf == NULL) {
		err(ctx, "%s: LSA buffer cannot be NULL\n", devname);
		return -EINVAL;
	}

	if (length == 0)
		return 0;

	if (memdev->payload_max < 0)
		return -EINVAL;

	label_iter_max = memdev->payload_max - sizeof(struct cxl_cmd_set_lsa);
	while (remaining) {
		cur_len = min((size_t)label_iter_max, remaining);
		rc = __lsa_op(memdev, op, buf + cur_off,
				cur_len, offset + cur_off);
		if (rc)
			break;

		remaining -= cur_len;
		cur_off += cur_len;
	}

	if (rc && (op == LSA_OP_SET))
		err(ctx, "%s: labels may be in an inconsistent state\n",
			devname);
	return rc;
}

CXL_EXPORT int cxl_memdev_zero_label(struct cxl_memdev *memdev, size_t length,
		size_t offset)
{
	return lsa_op(memdev, LSA_OP_ZERO, NULL, length, offset);
}

CXL_EXPORT int cxl_memdev_write_label(struct cxl_memdev *memdev, void *buf,
		size_t length, size_t offset)
{
	return lsa_op(memdev, LSA_OP_SET, buf, length, offset);
}

CXL_EXPORT int cxl_memdev_read_label(struct cxl_memdev *memdev, void *buf,
		size_t length, size_t offset)
{
	return lsa_op(memdev, LSA_OP_GET, buf, length, offset);
}

#define cxl_alert_config_set_field(field)                                     \
CXL_EXPORT int cxl_cmd_alert_config_set_##field(struct cxl_cmd *cmd, int val) \
{                                                                             \
	struct cxl_cmd_set_alert_config *setalert = cmd->input_payload;       \
	setalert->field = val;                                                \
	return 0;                                                             \
}

cxl_alert_config_set_field(life_used_prog_warn_threshold)
cxl_alert_config_set_field(dev_over_temperature_prog_warn_threshold)
cxl_alert_config_set_field(dev_under_temperature_prog_warn_threshold)
cxl_alert_config_set_field(corrected_volatile_mem_err_prog_warn_threshold)
cxl_alert_config_set_field(corrected_pmem_err_prog_warn_threshold)
cxl_alert_config_set_field(valid_alert_actions)
cxl_alert_config_set_field(enable_alert_actions)

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_set_alert_config(struct cxl_memdev *memdev)
{
	return cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_SET_ALERT_CONFIG);
}
