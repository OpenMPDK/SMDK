// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include <linux/mutex.h>
#include <linux/pnm/log.h>
#include <linux/pnm/sched.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#define BUSY_STATE (1)
#define IDLE_STATE (0)

void pnm_sched_lock(struct pnm_sched *sched)
{
	mutex_lock(&sched->lock);
}
EXPORT_SYMBOL(pnm_sched_lock);

void pnm_sched_unlock(struct pnm_sched *sched)
{
	mutex_unlock(&sched->lock);
}
EXPORT_SYMBOL(pnm_sched_unlock);

void pnm_sched_reset_lock(struct pnm_sched *sched)
{
	if (unlikely(mutex_is_locked(&sched->lock))) {
		PNM_ERR("Mutex unlock forced.\n");
		// [TODO: @p.bred] Question about UB exclusion, see pnm_alloc.c
		pnm_sched_unlock(sched);
	}
}
EXPORT_SYMBOL(pnm_sched_reset_lock);

/* should be used under sched mutex only */
static ulong pnm_sched_find_free_cunits(struct pnm_sched *sched, ulong req_msk)
{
	size_t cunit;
	ulong msk = 0;

	for (cunit = 0; cunit < sched->nr_cunits; cunit++) {
		if (test_bit(cunit, &req_msk) &&
		    sched->states[cunit].state == IDLE_STATE)
			set_bit(cunit, &msk);
	}

	PNM_DBG("Find free cunits: msk = 0x%lx for req_msk = 0x%lx", msk,
		req_msk);

	return msk;
}

static void pnm_sched_reset_states(struct pnm_sched *sched)
{
	uint8_t cunit;

	for (cunit = 0; cunit < sched->nr_cunits; cunit++) {
		sched->states[cunit].state = IDLE_STATE;
		atomic64_set(&sched->states[cunit].acquisition_cnt, 0);
	}
}

static void pnm_sched_wakeup_queue(struct pnm_sched *sched)
{
	atomic_set(&sched->wait_flag, 1);
	wake_up_interruptible(&sched->wq);
}

int pnm_sched_init(struct pnm_sched *sched, uint8_t nr_cunits, uint64_t timeout)
{
	PNM_DBG("Initializing pnm scheduler\n");

	sched->nr_cunits = nr_cunits;

	sched->states =
		kcalloc(nr_cunits, sizeof(struct cunit_state), GFP_KERNEL);
	if (!sched->states)
		return -ENOMEM;
	pnm_sched_reset_states(sched);

	init_waitqueue_head(&sched->wq);
	atomic_set(&sched->wait_flag, 0);
	atomic64_set(&sched->retry_timeout_ns, timeout);
	mutex_init(&sched->lock);

	return 0;
}
EXPORT_SYMBOL(pnm_sched_init);

void pnm_sched_reset(struct pnm_sched *sched)
{
	PNM_DBG("Resetting pnm scheduler\n");
	pnm_sched_reset_lock(sched);
	pnm_sched_reset_states(sched);
	pnm_sched_wakeup_queue(sched);
}
EXPORT_SYMBOL(pnm_sched_reset);

void pnm_sched_cleanup(struct pnm_sched *sched)
{
	PNM_DBG("Cleanup pnm scheduler\n");
	pnm_sched_reset_lock(sched);
	mutex_destroy(&sched->lock);
	kfree(sched->states);
}
EXPORT_SYMBOL(pnm_sched_cleanup);

/* should be used under sched mutex only */
static uint8_t pnm_sched_acquire_free_cunit(struct pnm_sched *sched, ulong msk)
{
	const uint8_t cunit = __ffs(msk);
	struct cunit_state *cstate = &sched->states[cunit];

	cstate->state = BUSY_STATE;
	atomic64_inc(&cstate->acquisition_cnt);

	return cunit;
}

static int pnm_sched_find_acquire_free_cunit(struct pnm_sched *sched,
					     ulong req_msk)
{
	int ret = -1;
	ulong msk = 0, max_msk_val = BIT_MASK(sched->nr_cunits);

	PNM_DBG("Acquiring cunit for req_msk 0x%lx\n", req_msk);

	if (req_msk >= max_msk_val) {
		PNM_ERR("Invalid req_msk value: 0x%lx\n", req_msk);
		return -1;
	}

	pnm_sched_lock(sched);
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(sched->states);
		msk = pnm_sched_find_free_cunits(sched, req_msk);
		if (msk)
			ret = pnm_sched_acquire_free_cunit(sched, msk);
	}
	pnm_sched_unlock(sched);

	if (ret < 0)
		PNM_DBG("No free cunit\n");
	else
		PNM_DBG("Acquired cunit %d\n", ret);

	return ret;
}

// Here we disable kcsan checks due to unavoidable data race in
// wait_event_interruptible_hrtimeout. We can't replace that function because the
// HW is hanging
static __no_kcsan int pnm_sched_wait_for_cunit_timeout(struct pnm_sched *sched)
{
	atomic_set(&sched->wait_flag, 0);
	/* timeout in nanoseconds */
	return wait_event_interruptible_hrtimeout(
		sched->wq, atomic_read(&sched->wait_flag),
		atomic64_read(&sched->retry_timeout_ns));
}

static int pnm_sched_wait_for_cunit_no_timeout(struct pnm_sched *sched)
{
	atomic_set(&sched->wait_flag, 0);
	return wait_event_interruptible(sched->wq,
					atomic_read(&sched->wait_flag));
}

static int pnm_sched_wait_for_cunit(struct pnm_sched *sched)
{
	if (atomic64_read(&sched->retry_timeout_ns) != PNM_SCHED_NO_TIMEOUT)
		return pnm_sched_wait_for_cunit_timeout(sched);

	return pnm_sched_wait_for_cunit_no_timeout(sched);
}

static int pnm_sched_retry_get_free_cunit(struct pnm_sched *sched,
					  ulong req_msk)
{
	int cunit = -1;

	while (cunit < 0)
		if (pnm_sched_wait_for_cunit(sched) >= 0)
			cunit = pnm_sched_find_acquire_free_cunit(sched,
								  req_msk);
		else
			return -1;

	return cunit;
}

int pnm_sched_get_free_cunit(struct pnm_sched *sched, ulong req_msk)
{
	int ret = pnm_sched_find_acquire_free_cunit(sched, req_msk);

	if (ret < 0) // was not able to get cunit, schedule retries
		return pnm_sched_retry_get_free_cunit(sched, req_msk);

	return ret;
}
EXPORT_SYMBOL(pnm_sched_get_free_cunit);

bool pnm_sched_get_cunit_state(struct pnm_sched *sched, uint8_t cunit)
{
	bool state;

	pnm_sched_lock(sched);
	state = sched->states[cunit].state == BUSY_STATE;
	pnm_sched_unlock(sched);
	return state;
}
EXPORT_SYMBOL(pnm_sched_get_cunit_state);

uint64_t pnm_sched_get_cunit_acquisition_cnt(const struct pnm_sched *sched,
					     uint8_t cunit)
{
	return atomic64_read(&sched->states[cunit].acquisition_cnt);
}
EXPORT_SYMBOL(pnm_sched_get_cunit_acquisition_cnt);

uint64_t pnm_sched_get_acquisition_timeout(const struct pnm_sched *sched)
{
	return atomic64_read(&sched->retry_timeout_ns);
}
EXPORT_SYMBOL(pnm_sched_get_acquisition_timeout);

void pnm_sched_set_acquisition_timeout(struct pnm_sched *sched,
				       uint64_t timeout)
{
	atomic64_set(&sched->retry_timeout_ns, timeout);
}
EXPORT_SYMBOL(pnm_sched_set_acquisition_timeout);

int pnm_sched_release_cunit(struct pnm_sched *sched, uint8_t cunit)
{
	int ret = cunit;

	PNM_DBG("Releasing cunit %hhu\n", cunit);

	if (cunit >= sched->nr_cunits) {
		PNM_ERR("Invalid cunit value: %hhu\n", cunit);
		return -1;
	}

	pnm_sched_lock(sched);
	{
		ASSERT_EXCLUSIVE_ACCESS_SCOPED(sched->states);
		sched->states[cunit].state = IDLE_STATE;
	}
	pnm_sched_unlock(sched);

	return ret;
}
EXPORT_SYMBOL(pnm_sched_release_cunit);

int pnm_sched_release_and_wakeup(struct pnm_sched *sched, uint8_t cunit)
{
	int ret = pnm_sched_release_cunit(sched, cunit);

	if (ret >= 0)
		pnm_sched_wakeup_queue(sched);

	return ret;
}
EXPORT_SYMBOL(pnm_sched_release_and_wakeup);
