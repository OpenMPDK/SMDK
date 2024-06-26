/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __IMDB_PARAMETERS_H__
#define __IMDB_PARAMETERS_H__

#include <linux/types.h>

struct imdb_params {
	int nr_pools;
	int nr_cunits;
	unsigned int mem_size_gb;
	phys_addr_t base_data_addr;
};

const struct imdb_params *imdb_topo(void);

#endif // __IMDB_PARAMETERS_H__
