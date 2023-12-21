/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#ifndef __SLS_PRIVATE_H__
#define __SLS_PRIVATE_H__

#include <linux/cdev.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/pnm/log.h>
#include <linux/semaphore.h>
#include <linux/sls_common.h>
#include <linux/types.h>

#define SLS_COPY_FROM_TO_USER(func, error, dst, src, size)        \
	do {                                                      \
		error = func(dst, src, size) ? -EFAULT : 0;       \
		if (unlikely(error)) {                            \
			PNM_ERR("Can't copy '" #src "' to '" #dst \
				"' in '" #func "'\n");            \
		}                                                 \
	} while (0)
#define SLS_COPY_FROM_USER(error, dst, src, size) \
	SLS_COPY_FROM_TO_USER(copy_from_user, error, dst, src, size)
#define SLS_COPY_TO_USER(error, dst, src, size) \
	SLS_COPY_FROM_TO_USER(copy_to_user, error, dst, src, size)

void cleanup_sls_device(void);

long sls_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

int sls_release(struct inode *node, struct file *f);
int sls_open(struct inode *inode, struct file *filp);

#endif

/* __SLS_PRIVATE_H__ */
