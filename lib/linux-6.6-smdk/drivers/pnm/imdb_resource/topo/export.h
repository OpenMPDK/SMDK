/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#ifndef __IMDB_TOPOLOGY_EXPORT__
#define __IMDB_TOPOLOGY_EXPORT__

#include <linux/kobject.h>

int imdb_export_topology_constants(struct kobject *resource_kobj);
void imdb_destroy_topology_constants(void);

#endif /* __IMDB_TOPOLOGY_EXPORT__ */
