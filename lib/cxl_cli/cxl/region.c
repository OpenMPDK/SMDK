// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020-2022 Intel Corporation. All rights reserved. */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <util/log.h>
#include <uuid/uuid.h>
#include <util/json.h>
#include <util/size.h>
#include <cxl/libcxl.h>
#include <json-c/json.h>
#include <util/parse-options.h>
#include <ccan/minmax/minmax.h>
#include <ccan/short_types/short_types.h>
#include <daxctl/libdaxctl.h>

#include "filter.h"
#include "json.h"

#ifdef ENABLE_SMDK_PLUGIN
#include "smdk/smdk_control.h"
#endif

static struct region_params {
	const char *bus;
	const char *size;
	const char *type;
	const char *uuid;
	const char *root_decoder;
	const char *region;
	int ways;
	int granularity;
	bool memdevs;
	bool force;
	bool human;
	bool debug;
	bool enforce_qos;
#ifdef ENABLE_SMDK_PLUGIN
	bool soft_interleaving;
	const char *group_preset;
	int target_node;
#endif
} param = {
	.ways = INT_MAX,
	.granularity = INT_MAX,
#ifdef ENABLE_SMDK_PLUGIN
	.target_node = -1,
#endif
};

struct parsed_params {
	u64 size;
	u64 ep_min_size;
	int ways;
	int granularity;
	uuid_t uuid;
	struct json_object *memdevs;
	int num_memdevs;
	int argc;
	const char **argv;
	struct cxl_decoder *root_decoder;
	enum cxl_decoder_mode mode;
	bool enforce_qos;
#ifdef ENABLE_SMDK_PLUGIN
	bool soft_interleaving;
	const char *group_preset;
	int target_node;
#endif
};

enum region_actions {
	ACTION_CREATE,
	ACTION_ENABLE,
	ACTION_DISABLE,
	ACTION_DESTROY,
};

static struct log_ctx rl;

#define BASE_OPTIONS() \
OPT_STRING('b', "bus", &param.bus, "bus name", \
	   "Limit operation to the specified bus"), \
OPT_STRING('d', "decoder", &param.root_decoder, "root decoder name", \
	   "Limit to / use the specified root decoder"), \
OPT_BOOLEAN(0, "debug", &param.debug, "turn on debug")

#define CREATE_OPTIONS() \
OPT_STRING('s', "size", &param.size, \
	   "size in bytes or with a K/M/G etc. suffix", \
	   "total size desired for the resulting region."), \
OPT_INTEGER('w', "ways", &param.ways, \
	    "number of memdevs participating in the regions interleave set"), \
OPT_INTEGER('g', "granularity", &param.granularity,  \
	    "granularity of the interleave set"), \
OPT_STRING('t', "type", &param.type, \
	   "region type", "region type - 'pmem' or 'ram'"), \
OPT_STRING('U', "uuid", &param.uuid, \
	   "region uuid", "uuid for the new region (default: autogenerate)"), \
OPT_BOOLEAN('m', "memdevs", &param.memdevs, \
	    "non-option arguments are memdevs"), \
OPT_BOOLEAN('u', "human", &param.human, "use human friendly number formats"), \
OPT_BOOLEAN('Q', "enforce-qos", &param.enforce_qos, "enforce qos_class match")

#ifdef ENABLE_SMDK_PLUGIN
#define CREATE_GROUP_OPTIONS() \
OPT_BOOLEAN('V', "soft_interleaving", &param.soft_interleaving, \
           "create soft-interelaving node(s)"), \
OPT_STRING('G', "group", &param.group_preset, \
          "group preset", "preset soft interleaving nodes configuration type (node or noop)"), \
OPT_INTEGER('N', "target_node", &param.target_node, \
           "soft-interleaving node id to add cxl devices followed")

#define DESTROY_GROUP_OPTIONS() \
OPT_BOOLEAN('V', "soft_interleaving", &param.soft_interleaving, \
	    "destory(remove) soft-interelaving node(s)"), \
OPT_INTEGER('N', "target_node", &param.target_node, \
	    "soft-interleaving node id to remove cxl devices followed"), \
OPT_INTEGER('w', "ways", &param.ways, \
	    "number of cxldevs participating in the soft-interleaving node")
#endif

static const struct option create_options[] = {
	BASE_OPTIONS(),
	CREATE_OPTIONS(),
#ifdef ENABLE_SMDK_PLUGIN
	CREATE_GROUP_OPTIONS(),
#endif
	OPT_END(),
};

static const struct option enable_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option disable_options[] = {
	BASE_OPTIONS(),
	OPT_BOOLEAN('f', "force", &param.force,
		    "attempt to offline memory before disabling the region"),
	OPT_END(),
};

static const struct option destroy_options[] = {
	BASE_OPTIONS(),
	OPT_BOOLEAN('f', "force", &param.force,
		    "destroy region even if currently active"),
#ifdef ENABLE_SMDK_PLUGIN
	DESTROY_GROUP_OPTIONS(),
#endif
	OPT_END(),
};

/*
 * Convert an array of strings that can be a mixture of single items, a
 * command separated list, or a space separated list, into a flattened
 * comma-separated string. That single string can then be used as a
 * filter argument to cxl_filter_walk(), or an ordering constraint for
 * json_object_array_sort()
 *
 * On entry @count is the number of elements in the strings array, on
 * exit, @count is the number of elements in the csv.
 */
static const char *to_csv(int *count, const char **strings)
{
	ssize_t len = 0, cursor = 0;
	char *csv, *list, *save;
	int i, new_count = 0;
	const char *arg;

	if (!*count)
		return NULL;

	for (i = 0; i < *count; i++) {
		/*
		 * An entry in strings may itself by a space or comma
		 * separated list, so decompose that for the final csv
		 */
		list = strdup(strings[i]);
		if (!list)
			return NULL;

		for (arg = strtok_r(list, which_sep(list), &save); arg;
		     arg = strtok_r(NULL, which_sep(list), &save)) {
			len += strlen(arg);
			new_count++;
		}

		free(list);

	}

	len += new_count + 1;
	csv = calloc(1, len);
	if (!csv)
		return NULL;
	for (i = 0; i < *count; i++) {
		list = strdup(strings[i]);
		if (!list) {
			free(csv);
			return NULL;
		}

		for (arg = strtok_r(list, which_sep(list), &save); arg;
		     arg = strtok_r(NULL, which_sep(list), &save)) {
			cursor += snprintf(csv + cursor, len - cursor, "%s%s",
					   arg, i + 1 < new_count ? "," : "");
			if (cursor >= len) {
				csv[len - 1] = 0;
				break;
			}
		}

		free(list);
	}
	*count = new_count;
	return csv;
}

static struct sort_context {
	const char *csv;
} sort_context;

static int memdev_filter_pos(struct json_object *jobj, const char *_csv)
{
	struct cxl_memdev *memdev = json_object_get_userdata(jobj);
	const char *sep = which_sep(_csv);
	char *csv, *save;
	const char *arg;
	int pos;

	csv = strdup(_csv);
	if (!csv)
		return -1;

	for (pos = 0, arg = strtok_r(csv, sep, &save); arg;
	     arg = strtok_r(NULL, sep, &save), pos++)
		if (util_cxl_memdev_filter(memdev, arg, NULL))
			break;
	free(csv);

	return pos;
}

static int memdev_sort(const void *a, const void *b)
{
	struct json_object **a_obj, **b_obj;
	int a_pos, b_pos;

	a_obj = (struct json_object **) a;
	b_obj = (struct json_object **) b;

	a_pos = memdev_filter_pos(*a_obj, sort_context.csv);
	b_pos = memdev_filter_pos(*b_obj, sort_context.csv);

	if (a_pos < 0 || b_pos < 0)
		return 0;

	return a_pos - b_pos;
}

static struct json_object *collect_memdevs(struct cxl_ctx *ctx,
					   const char *decoder, int *count,
					   const char **mems)
{
	const char *csv = to_csv(count, mems);
	struct cxl_filter_params filter_params = {
		.decoder_filter = decoder,
		.memdevs = true,
		.memdev_filter = csv,
	};
	struct json_object *jmemdevs;

	jmemdevs = cxl_filter_walk(ctx, &filter_params);

	if (!jmemdevs) {
		log_err(&rl, "failed to retrieve memdevs\n");
		goto out;
	}

	if (json_object_array_length(jmemdevs) == 0) {
		log_err(&rl,
			"no active memdevs found: decoder: %s filter: %s\n",
			decoder, csv ? csv : "none");
		json_object_put(jmemdevs);
		jmemdevs = NULL;
		goto out;
	}

	if (csv) {
		sort_context.csv = csv,
		json_object_array_sort(jmemdevs, memdev_sort);
	}
out:
	free((void *)csv);
	return jmemdevs;
}

static bool validate_ways(struct parsed_params *p, int count)
{
	/*
	 * Validate interleave ways against targets found in the topology. If
	 * the targets were specified, then non-default param.ways must equal
	 * that number of targets.
	 */
	if (p->ways > p->num_memdevs || (count && p->ways != p->num_memdevs)) {
		log_err(&rl,
			"Interleave ways %d is %s than number of memdevs %s: %d\n",
			p->ways, p->ways > p->num_memdevs ? "greater" : "less",
			count ? "specified" : "found", p->num_memdevs);
		return false;
	}
	return true;
}

#ifdef ENABLE_SMDK_PLUGIN
static int parse_destory_options(struct cxl_ctx *ctx, int count,
				 const char **mems, struct parsed_params *p)
{
	if (param.soft_interleaving) {
		if (param.target_node != -1 && (count)) {
			log_err(&rl, "must not specify cxl devices or ways\n");
			return -EINVAL;
		}
		if (param.target_node == -1 && !count) {
			log_err(&rl,
				"must specify cxl devices e.g. cxl0, cxl1, ...\n");
			return -EINVAL;
		}
		if (param.target_node == -1 && param.ways != count) {
			log_err(&rl,
				"number of cxl devices should be the same with the value 'ways(-w)'\n");
			return -EINVAL;
		}

		p->soft_interleaving = true;
		p->ways = (count) ? count : 0;
		p->target_node = param.target_node;
	}

	return 0;
}
#endif

static int parse_create_options(struct cxl_ctx *ctx, int count,
				const char **mems, struct parsed_params *p)
{
#ifdef ENABLE_SMDK_PLUGIN
	if (param.soft_interleaving) {
		if (param.target_node == -1 && param.group_preset == NULL) {
			log_err(&rl,
				"no target node / group preset specified\n");
			return -EINVAL;
		}
		if (param.group_preset != NULL) {
			if (strncmp(param.group_preset, "node", 4) &&
			    strncmp(param.group_preset, "noop", 4)) {
				log_err(&rl,
					"group preset should be 'node' or 'noop'\n");
				return -EINVAL;
			}
			if (count) {
				log_err(&rl,
					"must not specify cxl devices or ways\n");
				return -EINVAL;
			}
		}
		if (param.target_node != -1 && !count) {
			log_err(&rl,
				"must specify cxl devices e.g. cxl0, cxl1, ...\n");
			return -EINVAL;
		}
		if (param.target_node != -1 && (param.ways != count)) {
			log_err(&rl,
				"number of cxl devices should be the same with the value 'ways(-w)'\n");
			return -EINVAL;
		}

		p->soft_interleaving = true;
		p->ways = (count) ? count : 0;
		p->group_preset = param.group_preset;
		p->target_node = param.target_node;

		return 0;
	}
#endif
	if (!param.root_decoder) {
		log_err(&rl, "no root decoder specified\n");
		return -EINVAL;
	}

	/*
	 * For all practical purposes, -m is the default target type, but hold
	 * off on actively making that decision until a second target option is
	 * available. Unless there are no arguments then just assume memdevs.
	 */
	if (!count)
		param.memdevs = true;

	if (!param.memdevs) {
		log_err(&rl,
			"must specify option for target object types (-m)\n");
		return -EINVAL;
	}

	/* Collect the valid memdevs relative to the given root decoder */
	p->memdevs = collect_memdevs(ctx, param.root_decoder, &count, mems);
	if (!p->memdevs)
		return -ENXIO;
	p->num_memdevs = json_object_array_length(p->memdevs);

	if (param.type) {
		p->mode = cxl_decoder_mode_from_ident(param.type);
		if (p->mode == CXL_DECODER_MODE_RAM && param.uuid) {
			log_err(&rl,
				"can't set UUID for ram / volatile regions");
			goto err;
		}
		if (p->mode == CXL_DECODER_MODE_NONE) {
			log_err(&rl, "unsupported type: %s\n", param.type);
			goto err;
		}
	} else {
		p->mode = CXL_DECODER_MODE_PMEM;
	}

	if (param.size) {
		p->size = parse_size64(param.size);
		if (p->size == ULLONG_MAX) {
			log_err(&rl, "Invalid size: %s\n", param.size);
			goto err;
		}
	}

	if (param.ways <= 0) {
		log_err(&rl, "Invalid interleave ways: %d\n", param.ways);
		goto err;
	} else if (param.ways < INT_MAX) {
		p->ways = param.ways;
		if (!validate_ways(p, count))
			goto err;
	} else if (count) {
		p->ways = count;
		if (!validate_ways(p, count))
			goto err;
	} else
		p->ways = p->num_memdevs;

	if (param.granularity < INT_MAX) {
		if (param.granularity <= 0) {
			log_err(&rl, "Invalid interleave granularity: %d\n",
				param.granularity);
			goto err;
		}
		p->granularity = param.granularity;
	}

	if (p->size && p->ways) {
		if (p->size % p->ways) {
			log_err(&rl,
				"size (%lu) is not an integral multiple of interleave-ways (%u)\n",
				p->size, p->ways);
			goto err;
		}
	}

	if (param.uuid) {
		if (uuid_parse(param.uuid, p->uuid)) {
			error("failed to parse uuid: '%s'\n", param.uuid);
			goto err;
		}
	}

	p->enforce_qos = param.enforce_qos;

	return 0;

err:
	json_object_put(p->memdevs);
	return -EINVAL;
}

static int parse_region_options(int argc, const char **argv,
				struct cxl_ctx *ctx, enum region_actions action,
				const struct option *options,
				struct parsed_params *p, const char *usage)
{
	const char * const u[] = {
		usage,
		NULL
	};

	argc = parse_options(argc, argv, options, u, 0);
	p->argc = argc;
	p->argv = argv;

	if (param.debug) {
		cxl_set_log_priority(ctx, LOG_DEBUG);
		rl.log_priority = LOG_DEBUG;
	} else
		rl.log_priority = LOG_INFO;

	switch(action) {
	case ACTION_CREATE:
		return parse_create_options(ctx, argc, argv, p);
#ifdef ENABLE_SMDK_PLUGIN
	case ACTION_DESTROY:
		return parse_destory_options(ctx, argc, argv, p);
#endif
	default:
		return 0;
	}
}

static void collect_minsize(struct cxl_ctx *ctx, struct parsed_params *p)
{
	int i;

	for (i = 0; i < p->ways; i++) {
		struct json_object *jobj =
			json_object_array_get_idx(p->memdevs, i);
		struct cxl_memdev *memdev = json_object_get_userdata(jobj);
		u64 size = 0;

		switch(p->mode) {
		case CXL_DECODER_MODE_RAM:
			size = cxl_memdev_get_ram_size(memdev);
			break;
		case CXL_DECODER_MODE_PMEM:
			size = cxl_memdev_get_pmem_size(memdev);
			break;
		default:
			/* Shouldn't ever get here */ ;
		}

		if (!p->ep_min_size)
			p->ep_min_size = size;
		else
			p->ep_min_size = min(p->ep_min_size, size);
	}
}

static int create_region_validate_qos_class(struct parsed_params *p)
{
	int root_qos_class;
	int qos_class;
	int i;

	if (!p->enforce_qos)
		return 0;

	root_qos_class = cxl_root_decoder_get_qos_class(p->root_decoder);
	if (root_qos_class == CXL_QOS_CLASS_NONE)
		return 0;

	for (i = 0; i < p->ways; i++) {
		struct json_object *jobj =
			json_object_array_get_idx(p->memdevs, i);
		struct cxl_memdev *memdev = json_object_get_userdata(jobj);

		if (p->mode == CXL_DECODER_MODE_RAM)
			qos_class = cxl_memdev_get_ram_qos_class(memdev);
		else
			qos_class = cxl_memdev_get_pmem_qos_class(memdev);

		/* No qos_class entries. Possibly no kernel support */
		if (qos_class == CXL_QOS_CLASS_NONE)
			break;

		if (qos_class != root_qos_class) {
			if (p->enforce_qos) {
				log_err(&rl, "%s qos_class mismatch %s\n",
					cxl_decoder_get_devname(p->root_decoder),
					cxl_memdev_get_devname(memdev));

				return -ENXIO;
			}
		}
	}

	return 0;
}

static int validate_decoder(struct cxl_decoder *decoder,
			    struct parsed_params *p)
{
	const char *devname = cxl_decoder_get_devname(decoder);
	int rc;

	switch(p->mode) {
	case CXL_DECODER_MODE_RAM:
		if (!cxl_decoder_is_volatile_capable(decoder)) {
			log_err(&rl, "%s is not volatile capable\n", devname);
			return -EINVAL;
		}
		break;
	case CXL_DECODER_MODE_PMEM:
		if (!cxl_decoder_is_pmem_capable(decoder)) {
			log_err(&rl, "%s is not pmem capable\n", devname);
			return -EINVAL;
		}
		break;
	default:
		log_err(&rl, "unknown type: %s\n", param.type);
		return -EINVAL;
	}

	rc = create_region_validate_qos_class(p);
	if (rc)
		return rc;

	/* TODO check if the interleave config is possible under this decoder */

	return 0;
}

static void set_type_from_decoder(struct cxl_ctx *ctx, struct parsed_params *p)
{
	/* if param.type was explicitly specified, nothing to do here */
	if (param.type)
		return;

	/*
	 * default to pmem if both types are set, otherwise the single
	 * capability dominates.
	 */
	if (cxl_decoder_is_volatile_capable(p->root_decoder))
		p->mode = CXL_DECODER_MODE_RAM;
	if (cxl_decoder_is_pmem_capable(p->root_decoder))
		p->mode = CXL_DECODER_MODE_PMEM;
}

static int create_region_validate_config(struct cxl_ctx *ctx,
					 struct parsed_params *p)
{
	struct cxl_bus *bus;
	int rc;

	cxl_bus_foreach(ctx, bus) {
		struct cxl_decoder *decoder;
		struct cxl_port *port;

		if (!util_cxl_bus_filter(bus, param.bus))
			continue;

		port = cxl_bus_get_port(bus);
		if (!cxl_port_is_root(port))
			continue;

		cxl_decoder_foreach (port, decoder) {
			if (util_cxl_decoder_filter(decoder,
						    param.root_decoder)) {
				p->root_decoder = decoder;
				goto found;
			}
		}
	}

found:
	if (p->root_decoder == NULL) {
		log_err(&rl, "%s not found in CXL topology\n",
			param.root_decoder);
		return -ENXIO;
	}

	set_type_from_decoder(ctx, p);

	rc = validate_decoder(p->root_decoder, p);
	if (rc)
		return rc;

	collect_minsize(ctx, p);
	return 0;
}

static struct cxl_decoder *cxl_memdev_find_decoder(struct cxl_memdev *memdev)
{
	const char *memdev_name = cxl_memdev_get_devname(memdev);
	struct cxl_decoder *decoder;
	struct cxl_endpoint *ep;
	struct cxl_port *port;

	ep = cxl_memdev_get_endpoint(memdev);
	if (!ep) {
		log_err(&rl, "could not get an endpoint for %s\n",
			memdev_name);
		return NULL;
	}

	port = cxl_endpoint_get_port(ep);
	if (!port) {
		log_err(&rl, "could not get a port for %s\n",
			memdev_name);
		return NULL;
	}

	cxl_decoder_foreach(port, decoder)
		if (cxl_decoder_get_size(decoder) == 0)
			return decoder;

	log_err(&rl, "could not get a free decoder for %s\n", memdev_name);
	return NULL;
}

#define try(prefix, op, dev, p) \
do { \
	int __rc = prefix##_##op(dev, p); \
	if (__rc) { \
		log_err(&rl, "%s: " #op " failed: %s\n", \
				prefix##_get_devname(dev), \
				strerror(abs(__rc))); \
		rc = __rc; \
		goto out; \
	} \
} while (0)

static int cxl_region_determine_granularity(struct cxl_region *region,
					    struct parsed_params *p)
{
	const char *devname = cxl_region_get_devname(region);
	int granularity, ways;

	/* Default granularity will be the root decoder's granularity */
	granularity = cxl_decoder_get_interleave_granularity(p->root_decoder);
	if (granularity == 0 || granularity == -1) {
		log_err(&rl, "%s: unable to determine root decoder granularity\n",
			devname);
		return -ENXIO;
	}

	/* If no user-supplied granularity, just use the default */
	if (!p->granularity)
		return granularity;

	ways = cxl_decoder_get_interleave_ways(p->root_decoder);
	if (ways == 0 || ways == -1) {
		log_err(&rl, "%s: unable to determine root decoder ways\n",
			devname);
		return -ENXIO;
	}

	/* For ways == 1, any user-supplied granularity is fine */
	if (ways == 1)
		return p->granularity;

	/*
	 * For ways > 1, only allow the same granularity as the selected
	 * root decoder
	 */
	if (p->granularity == granularity)
		return granularity;

	log_err(&rl,
		"%s: For an x%d root, only root decoder granularity (%d) permitted\n",
		devname, ways, granularity);
	return -EINVAL;
}

static int create_region(struct cxl_ctx *ctx, int *count,
			 struct parsed_params *p)
{
	unsigned long flags = UTIL_JSON_TARGETS;
	struct json_object *jregion;
	struct cxl_region *region;
	bool default_size = true;
	int i, rc, granularity;
	u64 size, max_extent;
	const char *devname;

	rc = create_region_validate_config(ctx, p);
	if (rc)
		return rc;

	if (p->size) {
		size = p->size;
		default_size = false;
	} else if (p->ep_min_size) {
		size = p->ep_min_size * p->ways;
	} else {
		log_err(&rl, "unable to determine region size\n");

		return -ENXIO;
	}
	max_extent = cxl_decoder_get_max_available_extent(p->root_decoder);
	if (max_extent == ULLONG_MAX) {
		log_err(&rl, "%s: unable to determine max extent\n",
			cxl_decoder_get_devname(p->root_decoder));
		return -EINVAL;
	}
	if (!default_size && size > max_extent) {
		log_err(&rl,
			"%s: region size %#lx exceeds max available space\n",
			cxl_decoder_get_devname(p->root_decoder), size);
		return -ENOSPC;
	}

	if (size > max_extent)
		size = ALIGN_DOWN(max_extent, SZ_256M * p->ways);

	if (p->mode == CXL_DECODER_MODE_PMEM) {
		region = cxl_decoder_create_pmem_region(p->root_decoder);
		if (!region) {
			log_err(&rl, "failed to create region under %s\n",
				param.root_decoder);
			return -ENXIO;
		}
	} else if (p->mode == CXL_DECODER_MODE_RAM) {
		region = cxl_decoder_create_ram_region(p->root_decoder);
		if (!region) {
			log_err(&rl, "failed to create region under %s\n",
				param.root_decoder);
			return -ENXIO;
		}
	} else {
		log_err(&rl, "region type '%s' is not supported\n",
			param.type);
		return -EOPNOTSUPP;
	}

	devname = cxl_region_get_devname(region);

	rc = cxl_region_determine_granularity(region, p);
	if (rc < 0)
		goto out;
	granularity = rc;

	try(cxl_region, set_interleave_granularity, region, granularity);
	try(cxl_region, set_interleave_ways, region, p->ways);
	if (p->mode == CXL_DECODER_MODE_PMEM) {
		if (!param.uuid)
			uuid_generate(p->uuid);
		try(cxl_region, set_uuid, region, p->uuid);
	}
	try(cxl_region, set_size, region, size);

	for (i = 0; i < p->ways; i++) {
		struct cxl_decoder *ep_decoder;
		struct json_object *jobj =
			json_object_array_get_idx(p->memdevs, i);
		struct cxl_memdev *memdev = json_object_get_userdata(jobj);

		ep_decoder = cxl_memdev_find_decoder(memdev);
		if (!ep_decoder) {
			rc = -ENXIO;
			goto out;
		}
		if (cxl_decoder_get_mode(ep_decoder) != p->mode) {
			/*
			 * The cxl_memdev_find_decoder() helper returns a free
			 * decoder whose size has been checked for 0.
			 * Thus it is safe to change the mode here if needed.
			 */
			try(cxl_decoder, set_dpa_size, ep_decoder, 0);
			try(cxl_decoder, set_mode, ep_decoder, p->mode);
		}
		try(cxl_decoder, set_dpa_size, ep_decoder, size/p->ways);
		rc = cxl_region_set_target(region, i, ep_decoder);
		if (rc) {
			log_err(&rl, "%s: failed to set target%d to %s\n",
				devname, i, cxl_memdev_get_devname(memdev));
			goto out;
		}
	}

	rc = cxl_region_decode_commit(region);
	if (rc) {
		log_err(&rl, "%s: failed to commit decode: %s\n", devname,
			strerror(-rc));
		goto out;
	}

	rc = cxl_region_enable(region);
	if (rc) {
		log_err(&rl, "%s: failed to enable: %s\n", devname,
			strerror(-rc));
		goto out;
	}
	*count = 1;

	if (isatty(1))
		flags |= UTIL_JSON_HUMAN;
	jregion = util_cxl_region_to_json(region, flags);
	if (jregion)
		printf("%s\n", json_object_to_json_string_ext(jregion,
					JSON_C_TO_STRING_PRETTY));

out:
	json_object_put(p->memdevs);
	if (rc)
		cxl_region_delete(region);
	return rc;
}

static int disable_region(struct cxl_region *region)
{
	const char *devname = cxl_region_get_devname(region);
	struct daxctl_region *dax_region;
	struct daxctl_memory *mem;
	struct daxctl_dev *dev;
	int failed = 0, rc;

	dax_region = cxl_region_get_daxctl_region(region);
	if (!dax_region)
		goto out;

	daxctl_dev_foreach(dax_region, dev) {
		if (!daxctl_dev_is_system_ram_capable(dev))
			continue;

		mem = daxctl_dev_get_memory(dev);
		if (!mem)
			return -ENXIO;

		/*
		 * If memory is still online and user wants to force it, attempt
		 * to offline it.
		 */
		if (daxctl_memory_is_online(mem)) {
			rc = daxctl_memory_offline(mem);
			if (rc < 0) {
				log_err(&rl, "%s: unable to offline %s: %s\n",
					devname,
					daxctl_dev_get_devname(dev),
					strerror(abs(rc)));
				if (!param.force)
					return rc;

				failed++;
			}
		}
	}

	if (failed) {
		log_err(&rl, "%s: Forcing region disable without successful offline.\n",
			devname);
		log_err(&rl, "%s: Physical address space has now been permanently leaked.\n",
			devname);
		log_err(&rl, "%s: Leaked address cannot be recovered until a reboot.\n",
			devname);
	}

out:
	return cxl_region_disable(region);
}

static int destroy_region(struct cxl_region *region)
{
	const char *devname = cxl_region_get_devname(region);
	unsigned int ways, i;
	int rc;

	/* First, unbind/disable the region if needed */
	if (cxl_region_is_enabled(region)) {
		if (param.force) {
			rc = disable_region(region);
			if (rc) {
				log_err(&rl, "%s: error disabling region: %s\n",
					devname, strerror(-rc));
				return rc;
			}
		} else {
			log_err(&rl, "%s active. Disable it or use --force\n",
				devname);
			return -EBUSY;
		}
	}

	/* Reset the region decode in preparation for removal */
	rc = cxl_region_decode_reset(region);
	if (rc) {
		log_err(&rl, "%s: failed to reset decode: %s\n", devname,
			strerror(-rc));
		return rc;
	}

	/* Reset all endpoint decoders and region targets */
	ways = cxl_region_get_interleave_ways(region);
	if (ways == 0 || ways == UINT_MAX) {
		log_err(&rl, "%s: error getting interleave ways\n", devname);
		return -ENXIO;
	}

	for (i = 0; i < ways; i++) {
		struct cxl_decoder *ep_decoder;

		ep_decoder = cxl_region_get_target_decoder(region, i);
		if (!ep_decoder)
			return -ENXIO;

		rc = cxl_region_clear_target(region, i);
		if (rc) {
			log_err(&rl, "%s: clearing target%d failed: %s\n",
				devname, i, strerror(abs(rc)));
			return rc;
		}

		rc = cxl_decoder_set_dpa_size(ep_decoder, 0);
		if (rc) {
			log_err(&rl, "%s: set_dpa_size failed: %s\n",
				cxl_decoder_get_devname(ep_decoder),
				strerror(abs(rc)));
			return rc;
		}
	}

	/* Finally, delete the region */
	return cxl_region_delete(region);
}

static int do_region_xable(struct cxl_region *region, enum region_actions action)
{
	switch (action) {
	case ACTION_ENABLE:
		return cxl_region_enable(region);
	case ACTION_DISABLE:
		return disable_region(region);
	case ACTION_DESTROY:
		return destroy_region(region);
	default:
		return -EINVAL;
	}
}

static int decoder_region_action(struct parsed_params *p,
				 struct cxl_decoder *decoder,
				 enum region_actions action, int *count)
{
	struct cxl_region *region, *_r;
	int rc = 0, err_rc = 0;

	cxl_region_foreach_safe (decoder, region, _r) {
		int i, match = 0;

		for (i = 0; i < p->argc; i++) {
			if (util_cxl_region_filter(region, p->argv[i])) {
				match = 1;
				break;
			}
		}
		if (!match)
			continue;

		rc = do_region_xable(region, action);
		if (rc == 0) {
			*count += 1;
		} else {
			log_err(&rl, "%s: failed: %s\n",
				cxl_region_get_devname(region), strerror(-rc));
			err_rc = rc;
		}
	}
	return err_rc ? err_rc : rc;
}

#ifdef ENABLE_SMDK_PLUGIN
static int region_action_soft_interleaving(enum region_actions action,
					   struct parsed_params *p, int *count)
{
	switch (action) {
	case ACTION_CREATE:
		if (p->group_preset == NULL) {
			return soft_interleaving_group_add(
				p->ways, p->argv, p->target_node, count);
		} else {
			if (!strncmp(p->group_preset, "node", 4)) {
				return soft_interleaving_group_node(count);
			} else { /* noop */
				return soft_interleaving_group_noop(count);
			}
		}
	case ACTION_DESTROY:
		return soft_interleaving_group_remove(p->ways, p->argv,
						      p->target_node, count);
	default:
		break;
	}
	return 0;
}
#endif

static int region_action(int argc, const char **argv, struct cxl_ctx *ctx,
			 enum region_actions action,
			 const struct option *options, struct parsed_params *p,
			 int *count, const char *u)
{
	int rc = 0, err_rc = 0;
	struct cxl_bus *bus;

	log_init(&rl, "cxl region", "CXL_REGION_LOG");
	rc = parse_region_options(argc, argv, ctx, action, options, p, u);
	if (rc)
		return rc;

#ifdef ENABLE_SMDK_PLUGIN
	if (p->soft_interleaving)
		return region_action_soft_interleaving(action, p, count);
#endif

	if (action == ACTION_CREATE)
		return create_region(ctx, count, p);

	cxl_bus_foreach(ctx, bus) {
		struct cxl_decoder *decoder;
		struct cxl_port *port;

		if (!util_cxl_bus_filter(bus, param.bus))
			continue;

		port = cxl_bus_get_port(bus);
		if (!cxl_port_is_root(port))
			continue;

		cxl_decoder_foreach (port, decoder) {
			if (!util_cxl_decoder_filter(decoder,
						     param.root_decoder))
				continue;
			rc = decoder_region_action(p, decoder, action, count);
			if (rc)
				err_rc = rc;
		}
	}

	if (err_rc) {
		log_err(&rl, "one or more failures, last failure: %s\n",
			strerror(-err_rc));
		return err_rc;
	}
	return rc;
}

int cmd_create_region(int argc, const char **argv, struct cxl_ctx *ctx)
{
	const char *u = "cxl create-region <target0> ... [<options>]";
	struct parsed_params p = { 0 };
	int rc, count = 0;

	rc = region_action(argc, argv, ctx, ACTION_CREATE, create_options, &p,
			   &count, u);
	log_info(&rl, "created %d region%s\n", count, count == 1 ? "" : "s");
	return rc == 0 ? 0 : EXIT_FAILURE;
}

int cmd_enable_region(int argc, const char **argv, struct cxl_ctx *ctx)
{
	const char *u = "cxl enable-region <region0> ... [<options>]";
	struct parsed_params p = { 0 };
	int rc, count = 0;

	rc = region_action(argc, argv, ctx, ACTION_ENABLE, enable_options, &p,
			   &count, u);
	log_info(&rl, "enabled %d region%s\n", count, count == 1 ? "" : "s");
	return rc == 0 ? 0 : EXIT_FAILURE;
}

int cmd_disable_region(int argc, const char **argv, struct cxl_ctx *ctx)
{
	const char *u = "cxl disable-region <region0> ... [<options>]";
	struct parsed_params p = { 0 };
	int rc, count = 0;

	rc = region_action(argc, argv, ctx, ACTION_DISABLE, disable_options, &p,
			   &count, u);
	log_info(&rl, "disabled %d region%s\n", count, count == 1 ? "" : "s");
	return rc == 0 ? 0 : EXIT_FAILURE;
}

int cmd_destroy_region(int argc, const char **argv, struct cxl_ctx *ctx)
{
	const char *u = "cxl destroy-region <region0> ... [<options>]";
	struct parsed_params p = { 0 };
	int rc, count = 0;

	rc = region_action(argc, argv, ctx, ACTION_DESTROY, destroy_options, &p,
			   &count, u);
	log_info(&rl, "destroyed %d region%s\n", count, count == 1 ? "" : "s");
	return rc == 0 ? 0 : EXIT_FAILURE;
}
