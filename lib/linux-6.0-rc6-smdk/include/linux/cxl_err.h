/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Advanced Micro Devices, Inc. */

#ifndef LINUX_CXL_ERR_H
#define LINUX_CXL_ERR_H

struct ras_capability_regs {
	u32 uncor_status;
	u32 uncor_mask;
	u32 uncor_severity;
	u32 cor_status;
	u32 cor_mask;
	u32 cap_control;
	u8 header_log[64];
};

#endif //__CXL_ERR_
