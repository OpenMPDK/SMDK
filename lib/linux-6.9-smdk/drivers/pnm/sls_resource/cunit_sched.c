// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022-2023 Samsung LTD. All rights reserved. */

#include "cunit_sched.h"
#include "process_manager.h"
#include "topo/params.h"

#include <linux/bitops.h>
#include <linux/pnm/log.h>
#include <linux/pnm/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

/* this timeout value was tunned for FPGA, in order to avoid hangs, which
 * presumably come from userspace cunit acquire attempts in a loop
 */
#define RETRY_TIMEOUT_NS (100000)

static struct pnm_sched sls_sched;

static int get_sls_cunit(struct file *filp, uint arg)
{
	int ret = pnm_sched_get_free_cunit(&sls_sched, arg);

	if (ret >= 0) /* got cunit, add to process resources */
		sls_proc_register_cunit(filp, ret);

	return ret;
}

static int release_sls_cunit(struct file *filp, uint arg)
{
	int ret = sls_proc_remove_cunit(filp, arg);

	if (likely(!ret))
		ret = pnm_sched_release_and_wakeup(&sls_sched, arg);

	return ret;
}

void reset_cunit_sched(void)
{
	PNM_DBG("Resetting sls scheduler\n");
	pnm_sched_reset(&sls_sched);
}

int init_cunit_sched(void)
{
	int err;

	PNM_DBG("Initializing sls scheduler\n");
	err = pnm_sched_init(&sls_sched, sls_topo()->nr_cunits,
			     RETRY_TIMEOUT_NS);

	if (err) {
		PNM_ERR("Failed to init sls scheduler\n");
		return err;
	}

	return 0;
}

void destroy_cunit_sched(void)
{
	PNM_DBG("Destroy sls scheduler\n");
	pnm_sched_cleanup(&sls_sched);
}

int cunit_sched_ioctl(struct file *filp, uint cmd, uint arg)
{
	switch (cmd) {
	case GET_CUNIT:
		return get_sls_cunit(filp, arg);
	case RELEASE_CUNIT:
		return release_sls_cunit(filp, arg);
	}

	return -ENOTTY;
}

bool cunit_sched_state(uint8_t cunit)
{
	return pnm_sched_get_cunit_state(&sls_sched, cunit);
}

uint64_t cunit_sched_acq_cnt(uint8_t cunit)
{
	return pnm_sched_get_cunit_acquisition_cnt(&sls_sched, cunit);
}

uint64_t cunit_sched_acq_timeout(void)
{
	return pnm_sched_get_acquisition_timeout(&sls_sched);
}

void cunit_sched_set_acq_timeout(uint64_t timeout)
{
	pnm_sched_set_acquisition_timeout(&sls_sched, timeout);
}

int release_cunit(uint8_t cunit)
{
	return pnm_sched_release_cunit(&sls_sched, cunit);
}
