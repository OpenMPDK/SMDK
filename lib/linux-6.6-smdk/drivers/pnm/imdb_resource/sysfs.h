/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include <linux/device.h>

int imdb_build_sysfs(struct device *imdb_resource);
void imdb_destroy_sysfs(struct device *dev);
