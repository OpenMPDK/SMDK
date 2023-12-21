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
#ifndef __SLS_RESOURCES_H__
#define __SLS_RESOURCES_H__

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif /* __KERNEL__ */

#include <linux/pnm_resources.h>

#define SLS_RESOURCE_DEVICE_NAME "sls_resource"

/* Path to SLS resource manager device within /dev */
#define SLS_RESOURCE_PATH_INTERNAL \
	PNM_RESOURCE_CLASS_NAME "/" SLS_RESOURCE_DEVICE_NAME

/* Path to SLS resource manager device */
#define SLS_RESOURCE_PATH \
	"/dev/" PNM_RESOURCE_CLASS_NAME "/" SLS_RESOURCE_DEVICE_NAME

/* Path to SLS memory device */
#define SLS_MEMDEV_PATH "/dev/sls_device"

/* Path to DAX device */
#define DAX_PATH "/dev/dax0.0"

/* Path to sls_resource sysfs root */
#define SLS_SYSFS_ROOT "/sys/class/" SLS_RESOURCE_PATH_INTERNAL

/* Path for mappings info */
#define DEVICE_MAPPINGS_PATH "mappings"

/* Block of sysfs paths for device manipulation and statistics */
/* SLS_RESOURCE_PATH/ */

/* O_WRONLY Path to reset device, write "1" for reset */
#define DEVICE_RESET_PATH "reset"
/* O_RDWR Path to acquisition timeout, measured in ns */
#define DEVICE_ACQUISITION_TIMEOUT_PATH "acq_timeout"
/*
 * O_RDWR Path to enabling/disabling cleanup of leaked resources by
 * resource manager, write "1" to enable and "0" to disable
 */
#define DEVICE_RESOURCE_CLEANUP_PATH "cleanup"
/*
 * O_RDONLY Path to read resource leakage status, holds number of processes
 * failed to free resources before exit
 */
#define DEVICE_LEAKED_PATH "leaked"

/* Block of sysfs paths for topology info, all O_RDONLY */
/* SLS_RESOURCE_PATH/topology */

/* Macro for constructing topology paths */
#define DEVICE_TOPOLOGY_PATH "topology"
#define DEVICE_TOPOLOGY_CONSTANT_PATH(constant) \
	DEVICE_TOPOLOGY_PATH "/" #constant

/* Device type */
#define ATTR_DEV_TYPE_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(dev_type)

/* Number of cunits */
#define ATTR_NUM_OF_CUNITS_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(nr_cunits)

/* Size of single instruction in bytes*/
#define ATTR_INSTRUCTION_SIZE_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(inst_sz)
/* Size of single element in sparse vector in bytes */
#define ATTR_DATA_SIZE_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(data_sz)
/* Size of TAG for sparse vector in bytes */
#define ATTR_ALIGNED_TAG_SIZE_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(aligned_tag_sz)

/* Offset for polling register */
#define ATTR_POLLING_REGISTER_OFFSET_PATH \
	DEVICE_TOPOLOGY_CONSTANT_PATH(reg_poll)
/* Value to execute SLS operation */
#define ATTR_SLS_EXEC_VALUE_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(reg_exec)
/* Offset for SLS-enable register */
#define ATTR_REG_SLS_EN_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(reg_en)

/* Instruction, Psum & Tags Buffers Size (Bytes) */
#define ATTR_BUFFER_SIZE_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(buf_sz)
/* The number of psum buffers */
#define ATTR_PSUM_BUFFER_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(nr_psum_buf)
/* The number and the size (in bytes) of tags buffers in order (number size) */
#define ATTR_TAGS_BUFFER_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(nr_tag_buf)
/* The number and the size (in bytes) of instruction buffers in order (number size) */
#define ATTR_INST_BUFFER_PATH DEVICE_TOPOLOGY_CONSTANT_PATH(nr_inst_buf)

/* Block of sysfs paths for cunits info, all O_RDONLY */
/* SLS_RESOURCE_PATH/cunit/%d */

/* Path for cunits info */
#define DEVICE_CUNITS_PATH "cunits"

/* O_RDONLY Rank state, 0 = free, 1 = busy */
#define CUNIT_STATE_PATH "state"
/* O_RDWR Rank acquisitions count */
#define CUNIT_ACQUISITION_COUNT_PATH "acquisition_count"
/* O_RDONLY Get free size in bytes */
#define CUNIT_FREE_SIZE_PATH "free_size"

#define CUNIT_REGION_SIZE_PATH(region) "regions/" #region "/size"
#define CUNIT_REGION_OFFSET_PATH(region) "regions/" #region "/offset"
#define CUNIT_REGION_MAP_SIZE_PATH(region) "regions/" #region "/map_size"
#define CUNIT_REGION_MAP_OFFSET_PATH(region) "regions/" #region "/map_offset"

/* BASE region's size and offset */
#define CUNIT_REGION_BASE_SIZE_PATH CUNIT_REGION_SIZE_PATH(base)
#define CUNIT_REGION_BASE_OFFSET_PATH CUNIT_REGION_OFFSET_PATH(base)
#define CUNIT_REGION_BASE_MAP_SIZE_PATH CUNIT_REGION_MAP_SIZE_PATH(base)
#define CUNIT_REGION_BASE_MAP_OFFSET_PATH CUNIT_REGION_MAP_OFFSET_PATH(base)
/* INST region's size and offset */
#define CUNIT_REGION_INST_SIZE_PATH CUNIT_REGION_SIZE_PATH(inst)
#define CUNIT_REGION_INST_OFFSET_PATH CUNIT_REGION_OFFSET_PATH(inst)
#define CUNIT_REGION_INST_MAP_SIZE_PATH CUNIT_REGION_MAP_SIZE_PATH(inst)
#define CUNIT_REGION_INST_MAP_OFFSET_PATH CUNIT_REGION_MAP_OFFSET_PATH(inst)
/* CFGR region's size and offset */
#define CUNIT_REGION_CFGR_SIZE_PATH CUNIT_REGION_SIZE_PATH(cfgr)
#define CUNIT_REGION_CFGR_OFFSET_PATH CUNIT_REGION_OFFSET_PATH(cfgr)
#define CUNIT_REGION_CFGR_MAP_SIZE_PATH CUNIT_REGION_MAP_SIZE_PATH(cfgr)
#define CUNIT_REGION_CFGR_MAP_OFFSET_PATH CUNIT_REGION_MAP_OFFSET_PATH(cfgr)
/* TAGS region's size and offset */
#define CUNIT_REGION_TAGS_SIZE_PATH CUNIT_REGION_SIZE_PATH(tags)
#define CUNIT_REGION_TAGS_OFFSET_PATH CUNIT_REGION_OFFSET_PATH(tags)
#define CUNIT_REGION_TAGS_MAP_SIZE_PATH CUNIT_REGION_MAP_SIZE_PATH(tags)
#define CUNIT_REGION_TAGS_MAP_OFFSET_PATH CUNIT_REGION_MAP_OFFSET_PATH(tags)
/* PSUM region's size and offset */
#define CUNIT_REGION_PSUM_SIZE_PATH CUNIT_REGION_SIZE_PATH(psum)
#define CUNIT_REGION_PSUM_OFFSET_PATH CUNIT_REGION_OFFSET_PATH(psum)
#define CUNIT_REGION_PSUM_MAP_SIZE_PATH CUNIT_REGION_MAP_SIZE_PATH(psum)
#define CUNIT_REGION_PSUM_MAP_OFFSET_PATH CUNIT_REGION_MAP_OFFSET_PATH(psum)

/* The enumeration of sls blocks addresses */
enum sls_mem_blocks_e {
	SLS_BLOCK_BASE = 0,
	SLS_BLOCK_INST = 1,
	SLS_BLOCK_CFGR = 2,
	SLS_BLOCK_TAGS = 3,
	SLS_BLOCK_PSUM = 4,
	SLS_BLOCK_MAX = 5
};

/* [TODO @e-kutovoi MCS23-1260]: Put these in userspace */

/* The enumeration of table allocation preferences */
enum sls_user_preferences {
	SLS_ALLOC_AUTO = 0,
	SLS_ALLOC_REPLICATE_ALL,
	SLS_ALLOC_DISTRIBUTE_ALL,
	SLS_ALLOC_SINGLE
};

#define SLS_USER_PREF_BITS 8
#define SLS_USER_CUNIT_BITS 8

#define SLS_USER_PREF_MASK ((1U << SLS_USER_PREF_BITS) - 1)
#define SLS_USER_CUNIT_MASK \
	(((1U << SLS_USER_CUNIT_BITS) - 1) << SLS_USER_PREF_BITS)

#define SLS_ALLOC_SINGLE_CUNIT(rank) \
	(SLS_ALLOC_SINGLE | ((rank + 1) << SLS_USER_PREF_BITS))

#define GET_ALLOC_POLICY(preference) (preference & SLS_USER_PREF_MASK)
#define GET_ALLOC_SINGLE_CUNIT_PREFERENCE(preference) \
	((preference & SLS_USER_CUNIT_MASK) >> SLS_USER_PREF_BITS)

#define SLS_ALLOC_ANY_CUNIT ((uint8_t)0xffU)

#define SLS_IOC_MAGIC 'T'

#define GET_CUNIT _IOW(SLS_IOC_MAGIC, 0, unsigned int)
#define RELEASE_CUNIT _IOW(SLS_IOC_MAGIC, 1, unsigned int)
#define ALLOCATE_MEMORY _IOWR(SLS_IOC_MAGIC, 2, struct pnm_allocation)
#define DEALLOCATE_MEMORY _IOW(SLS_IOC_MAGIC, 3, struct pnm_allocation)
#define MAKE_SHARED_ALLOC _IOWR(SLS_IOC_MAGIC, 4, struct pnm_allocation)
#define GET_SHARED_ALLOC _IOWR(SLS_IOC_MAGIC, 5, struct pnm_allocation)

/* Required to switch CPU context to avoid hangs in SLS-CXL hardware */
/* [TODO: @y-lavrinenko] Get rid when firmware be more stable */
#define NOP _IO(SLS_IOC_MAGIC, 6)

#define SLS_IOC_MAXNR (6)

#endif /* __SLS_RESOURCES_H__ */
