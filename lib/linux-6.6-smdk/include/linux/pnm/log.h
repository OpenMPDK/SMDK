/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#ifndef __PNM_LOG_H__
#define __PNM_LOG_H__

#include <linux/kernel.h>

#ifndef PNM_LOG_COMPONENT_MARK
#error "PNM log component mark must be set"
#endif
#define PNM_LOG_MARK "[PNM]" PNM_LOG_COMPONENT_MARK

#define PNM_PRINT(level, fmt, ...)                                  \
	pr_##level(PNM_LOG_MARK "[%s:%d] " fmt, __FILE__, __LINE__, \
		   ##__VA_ARGS__)
#define PNM_ERR(fmt, ...) PNM_PRINT(err, fmt, ##__VA_ARGS__)
#define PNM_WRN(fmt, ...) PNM_PRINT(warn, fmt, ##__VA_ARGS__)
#define PNM_INF(fmt, ...) PNM_PRINT(info, fmt, ##__VA_ARGS__)
#define PNM_DBG(fmt, ...) PNM_PRINT(debug, fmt, ##__VA_ARGS__)

#endif /* __PNM_LOG_H__ */
