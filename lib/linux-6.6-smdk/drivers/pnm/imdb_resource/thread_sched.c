// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021-2023 Samsung LTD. All rights reserved. */

#include "thread_sched.h"
#include "proc_mgr.h"
#include "topo/params.h"

#include <linux/fs.h>
#include <linux/imdb_resources.h>
#include <linux/kernel.h>
#include <linux/pnm/log.h>
#include <linux/pnm/sched.h>

static struct pnm_sched thread_sched;

bool get_thread_state(uint8_t thread)
{
	return pnm_sched_get_cunit_state(&thread_sched, thread);
}

void reset_thread_sched(void)
{
	PNM_DBG("Thread scheduler reset\n");
	pnm_sched_reset(&thread_sched);
}

int init_thread_sched(void)
{
	PNM_DBG("Thread scheduler init\n");
	return pnm_sched_init(&thread_sched, imdb_topo()->nr_cunits,
			      PNM_SCHED_NO_TIMEOUT);
}

void destroy_thread_sched(void)
{
	PNM_DBG("Thread scheduler destroy\n");
	pnm_sched_cleanup(&thread_sched);
}

int thread_sched_ioctl(struct file *filp, uint cmd, ulong __user arg)
{
	uint8_t thread = 0;
	int ret = -EINVAL;

	switch (cmd) {
	case IMDB_IOCTL_GET_THREAD: {
		ret = pnm_sched_get_free_cunit(
			&thread_sched, GENMASK(imdb_topo()->nr_cunits - 1, 0));
		if (likely(ret >= 0)) {
			thread = ret;
			ret = imdb_register_thread(filp, thread);
			if (likely(!ret))
				return thread;
			pnm_sched_release_and_wakeup(&thread_sched, thread);
		}
		return ret;
	}
	case IMDB_IOCTL_RELEASE_THREAD: {
		ret = imdb_unregister_thread(filp, arg);
		if (likely(!ret))
			ret = pnm_sched_release_and_wakeup(&thread_sched, arg);
	}
	}

	return ret;
}

int thread_sched_clear_res(uint8_t thread)
{
	return pnm_sched_release_and_wakeup(&thread_sched, thread);
}
