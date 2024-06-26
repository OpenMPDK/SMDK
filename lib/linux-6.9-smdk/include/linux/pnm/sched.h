/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __PNM_SCHEDULER_H__
#define __PNM_SCHEDULER_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>

/*
 * If we initialize the PNM scheduler with a zero timeout,
 * the timeout is not used internally while waiting.
 */
#define PNM_SCHED_NO_TIMEOUT 0

/*
 * Add padding for cache alignment for scheduler fields, especially for state
 * counters to avoid false sharing. Surprisingly this solves FPGA hangs on
 * workloads with high counters contentions.
 * Perhaps this should be checked later more deeply with FPGA again.
 */
#define PNM_CACHE_ALIGN __aligned(L1_CACHE_BYTES)

struct pnm_sched {
	PNM_CACHE_ALIGN struct cunit_state {
		PNM_CACHE_ALIGN uint8_t state;
		PNM_CACHE_ALIGN atomic64_t acquisition_cnt;
	} *states;
	PNM_CACHE_ALIGN uint8_t nr_cunits;
	PNM_CACHE_ALIGN wait_queue_head_t wq;
	PNM_CACHE_ALIGN atomic_t wait_flag;
	PNM_CACHE_ALIGN atomic64_t retry_timeout_ns;
	PNM_CACHE_ALIGN struct mutex lock;
};

void pnm_sched_lock(struct pnm_sched *sched);
void pnm_sched_unlock(struct pnm_sched *sched);
void pnm_sched_reset_lock(struct pnm_sched *sched);

int pnm_sched_init(struct pnm_sched *sched, uint8_t nr_cunits,
		   uint64_t timeout);
void pnm_sched_reset(struct pnm_sched *sched);
void pnm_sched_cleanup(struct pnm_sched *sched);

int pnm_sched_get_free_cunit(struct pnm_sched *sched, ulong req_msk);
bool pnm_sched_get_cunit_state(struct pnm_sched *sched, uint8_t cunit);
uint64_t pnm_sched_get_cunit_acquisition_cnt(const struct pnm_sched *sched,
					     uint8_t cunit);
uint64_t pnm_sched_get_acquisition_timeout(const struct pnm_sched *sched);
void pnm_sched_set_acquisition_timeout(struct pnm_sched *sched,
				       uint64_t timeout);
int pnm_sched_release_cunit(struct pnm_sched *sched, uint8_t cunit);
int pnm_sched_release_and_wakeup(struct pnm_sched *sched, uint8_t cunit);

#endif /* __PNM_SCHEDULER_H__ */
