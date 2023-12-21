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
#ifndef __PNM_RESOURCES_H__
#define __PNM_RESOURCES_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h> /* for uint64_t */
#endif

#define PNM_RESOURCE_CLASS_NAME "pnm"

#pragma pack(push, 1)
struct pnm_allocation {
	uint64_t addr;
	uint64_t size;
	uint8_t memory_pool;
	/* [TODO: MCS23-1724] */
	/* Remove is_global after changes in process manager */
	uint8_t is_global;
};
#pragma pack(pop)

#endif /* __PNM_RESOURCES_H__ */
