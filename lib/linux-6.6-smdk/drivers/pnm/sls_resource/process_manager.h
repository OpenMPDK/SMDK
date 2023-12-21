/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022 Samsung LTD. All rights reserved. */

#ifndef __SLS_PROCESS_MANAGER_H__
#define __SLS_PROCESS_MANAGER_H__

#include "allocator.h"
#include "cunit_sched.h"

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>

void cleanup_process_manager(void);
void reset_process_manager(void);
/* function for handling 'open' file operation */
int register_sls_process(struct file *filp);
/* function for handling 'release' file operation */
int release_sls_process(struct file *filp);

/* functions for adding allocation/cunits into process's resources data structure*/
int sls_proc_register_alloc(struct file *filp, struct pnm_allocation alloc);
int sls_proc_register_cunit(struct file *filp, uint8_t cunit);

/* function for removing allocation/cunits from process's resources data structure*/
int sls_proc_remove_alloc(struct file *filp, struct pnm_allocation alloc);
int sls_proc_remove_cunit(struct file *filp, uint8_t cunit);

int sls_proc_manager_cleanup_on(void);
void sls_proc_manager_cleanup_off(void);

uint64_t sls_proc_mgr_leaked(void);
bool sls_proc_mgr_cleanup(void);

#endif /* __SLS_PROCESS_MANAGER_H__ */
