// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include "params.h"
#include "device_resource.h"

#include <linux/module.h>
#include <linux/moduleparam.h>

static struct imdb_params imdb_params = {
	.nr_pools = 2,
	.nr_cunits = 3,
	.mem_size_gb = 32,

	// [TODO: @alex.antonov] make base_addr configurable by cxl
	.base_data_addr = 66ULL << ONE_GB_SHIFT, // hardware base addr
};

const struct imdb_params *imdb_topo(void)
{
	return &imdb_params;
}

static int set_base_data_addr(const char *val, const struct kernel_param *kp)
{
	phys_addr_t value;
	int res;

	if (val) {
		res = kstrtoll(val, 0, &value);
		if (res != 0)
			return res;
		imdb_params.base_data_addr = value << ONE_GB_SHIFT;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int get_base_data_addr(char *buffer, const struct kernel_param *kp)
{
	const phys_addr_t value = imdb_params.base_data_addr >> ONE_GB_SHIFT;

	return sysfs_emit(buffer, "%llu\n", value);
}

static const struct kernel_param_ops param_ops_base_data_addr = {
	.set = set_base_data_addr,
	.get = get_base_data_addr,
};

#define NAMED_PARAM(param, type) \
	module_param_named(param, imdb_params.param, type, 0444)

NAMED_PARAM(nr_pools, int);
NAMED_PARAM(nr_cunits, int);
NAMED_PARAM(mem_size_gb, int);

#define param_check_base_data_addr(name, p) __param_check(name, p, phys_addr_t)
NAMED_PARAM(base_data_addr, base_data_addr);
