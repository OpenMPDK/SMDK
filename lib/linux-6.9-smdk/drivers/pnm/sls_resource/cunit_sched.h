/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022 Samsung LTD. All rights reserved. */

#ifndef __SLS_CUNIT_SCHEDULER_H__
#define __SLS_CUNIT_SCHEDULER_H__

#include <linux/fs.h>
#include <linux/sls_resources.h>
#include <linux/types.h>

void reset_cunit_sched(void);
int init_cunit_sched(void);
void destroy_cunit_sched(void);
int cunit_sched_ioctl(struct file *filp, uint cmd, uint arg);

/* these functions are intended for direct cunit status manipulation from
 * resource manager, in order to free resources which were not freed by
 * user space process itself
 */
int release_cunit(uint8_t cunit);

bool cunit_sched_state(uint8_t cunit);
uint64_t cunit_sched_acq_cnt(uint8_t cunit);

uint64_t cunit_sched_acq_timeout(void);
void cunit_sched_set_acq_timeout(uint64_t timeout);

#endif /* __SLS_CUNIT_SCHEDULER_H__ */
