/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#ifndef __THREAD_SCHED_H__
#define __THREAD_SCHED_H__

#include <linux/imdb_resources.h>
#include <linux/mutex.h>
#include <linux/wait.h>

int init_thread_sched(void);
void destroy_thread_sched(void);
void reset_thread_sched(void);

int thread_sched_ioctl(struct file *filp, uint cmd, ulong __user arg);

bool get_thread_state(uint8_t thread);

int thread_sched_clear_res(uint8_t thread);
#endif //__THREAD_SCHED_H__
