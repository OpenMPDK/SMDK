/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021 Intel Corporation. All rights reserved. */
#ifndef _CXL_UTIL_FILTER_H_
#define _CXL_UTIL_FILTER_H_

#include <stdbool.h>
#include <util/log.h>
#include <util/json.h>

struct cxl_filter_params {
	const char *memdev_filter;
	const char *serial_filter;
	const char *bus_filter;
	const char *port_filter;
	const char *endpoint_filter;
	const char *decoder_filter;
	const char *region_filter;
	bool single;
	bool endpoints;
	bool decoders;
	bool regions;
	bool targets;
	bool memdevs;
	bool ports;
	bool buses;
	bool idle;
	bool human;
	bool health;
	bool partition;
	bool fw;
	bool alert_config;
	bool dax;
	bool media_errors;
#ifdef ENABLE_SMDK_PLUGIN
	bool soft_interleaving;
	bool list_node;
	bool list_dev;
#endif
	int verbose;
	struct log_ctx ctx;
};

struct cxl_memdev *util_cxl_memdev_filter(struct cxl_memdev *memdev,
					  const char *__ident,
					  const char *serials);
struct cxl_memdev *util_cxl_memdev_filter_by_bus(struct cxl_memdev *memdev,
						 const char *__ident);
struct cxl_port *util_cxl_port_filter_by_memdev(struct cxl_port *port,
						const char *ident,
						const char *serial);
struct cxl_decoder *util_cxl_decoder_filter(struct cxl_decoder *decoder,
					    const char *__ident);
struct cxl_region *util_cxl_region_filter(struct cxl_region *region,
					  const char *__ident);

enum cxl_port_filter_mode {
	CXL_PF_SINGLE,
	CXL_PF_ANCESTRY,
};

struct cxl_port *util_cxl_port_filter(struct cxl_port *port, const char *ident,
				      enum cxl_port_filter_mode mode);
struct cxl_bus *util_cxl_bus_filter(struct cxl_bus *bus, const char *__ident);
struct cxl_endpoint *util_cxl_endpoint_filter(struct cxl_endpoint *endpoint,
					      const char *__ident);
struct cxl_target *util_cxl_target_filter_by_memdev(struct cxl_target *target,
						    const char *ident,
						    const char *serial);
struct cxl_dport *util_cxl_dport_filter_by_memdev(struct cxl_dport *dport,
						  const char *ident,
						  const char *serial);
struct cxl_decoder *util_cxl_decoder_filter(struct cxl_decoder *decoder,
					    const char *__ident);
struct json_object *cxl_filter_walk(struct cxl_ctx *ctx,
				    struct cxl_filter_params *param);

static inline unsigned long cxl_filter_to_flags(struct cxl_filter_params *param)
{
	unsigned long flags = 0;

	if (param->idle)
		flags |= UTIL_JSON_IDLE;
	if (param->human)
		flags |= UTIL_JSON_HUMAN;
	if (param->health)
		flags |= UTIL_JSON_HEALTH;
	if (param->targets)
		flags |= UTIL_JSON_TARGETS;
	if (param->partition)
		flags |= UTIL_JSON_PARTITION;
	if (param->fw)
		flags |= UTIL_JSON_FIRMWARE;
	if (param->alert_config)
		flags |= UTIL_JSON_ALERT_CONFIG;
	if (param->dax)
		flags |= UTIL_JSON_DAX | UTIL_JSON_DAX_DEVS;
	if (param->media_errors)
		flags |= UTIL_JSON_MEDIA_ERRORS;
	return flags;
}

bool cxl_filter_has(const char *needle, const char *__filter);
#endif /* _CXL_UTIL_FILTER_H_ */
