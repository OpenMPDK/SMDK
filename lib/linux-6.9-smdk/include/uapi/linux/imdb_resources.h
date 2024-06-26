/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2023 Samsung Electronics Co. LTD
 *
 * This software is proprietary of Samsung Electronics.
 * No part of this software, either material or conceptual may be copied or
 * distributed, transmitted, transcribed, stored in a retrieval system or
 * translated into any human or computer language in any form by any means,
 * electronic, mechanical, manual or otherwise, or disclosed to third parties
 * without the express written permission of Samsung Electronics.
 */
#ifndef __IMDB_RESOURCES_H__
#define __IMDB_RESOURCES_H__

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif /* __KERNEL__ */

#include <linux/pnm_resources.h>

// [TODO: y-lavrinenko] Should be merged with libimdb.h when the last is on kernel

#define IMDB_RESOURCE_DEVICE_NAME "imdb_resource"
#define IMDB_RESOURCE_DEVICE_PATH \
	"/dev/" PNM_RESOURCE_CLASS_NAME "/" IMDB_RESOURCE_DEVICE_NAME

#define IMDB_SYSFS_PATH \
	"/sys/class/" PNM_RESOURCE_CLASS_NAME "/" IMDB_RESOURCE_DEVICE_NAME

#define IMDB_DEVICE_TOPOLOGY_PATH "topology"
#define DEVICE_TOPOLOGY_CONSTANT_PATH(constant) \
	IMDB_DEVICE_TOPOLOGY_PATH "/" #constant

#define IMDB_ATTR_NUM_OF_RANKS_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(nr_pools)

#define IMDB_ATTR_NUM_OF_THREADS_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(nr_cunits)

#define IMDB_THREAD_BUSY 1
#define IMDB_THREAD_IDLE 0

#define IMDB_RESOURCE_IOC_MAGIC 'D'

#define IMDB_IOCTL_ALLOCATE \
	_IOWR(IMDB_RESOURCE_IOC_MAGIC, 1, struct pnm_allocation)
#define IMDB_IOCTL_DEALLOCATE \
	_IOW(IMDB_RESOURCE_IOC_MAGIC, 2, struct pnm_allocation)
#define IMDB_IOCTL_GET_THREAD _IO(IMDB_RESOURCE_IOC_MAGIC, 3)
#define IMDB_IOCTL_RELEASE_THREAD _IOW(IMDB_RESOURCE_IOC_MAGIC, 4, uint8_t)
#define IMDB_IOCTL_MAKE_SHARED_ALLOC \
	_IOWR(IMDB_RESOURCE_IOC_MAGIC, 5, struct pnm_allocation)
#define IMDB_IOCTL_GET_SHARED_ALLOC \
	_IOWR(IMDB_RESOURCE_IOC_MAGIC, 6, struct pnm_allocation)

#endif /* __IMDB_RESOURCES_H__ */
