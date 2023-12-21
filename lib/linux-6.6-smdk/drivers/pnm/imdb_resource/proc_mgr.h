/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __IMDB_PROC_MGR_H__
#define __IMDB_PROC_MGR_H__

#include <linux/fs.h>
#include <linux/imdb_resources.h>

#define IMDB_ENABLE_CLEANUP 1
#define IMDB_DISABLE_CLEANUP 0

int imdb_register_process(struct file *filp);
int imdb_release_process(struct file *filp);

int imdb_register_allocation(struct file *filp,
			     const struct pnm_allocation *alloc);
int imdb_unregister_allocation(struct file *filp,
			       const struct pnm_allocation *alloc);

int imdb_register_thread(struct file *filp, uint8_t thread);
int imdb_unregister_thread(struct file *filp, uint8_t thread);

uint64_t imdb_get_leaked(void);

int imdb_enable_cleanup(void);
void imdb_disable_cleanup(void);
bool imdb_get_proc_manager(void);

int imdb_reset_proc_manager(void);

void imdb_destroy_proc_manager(void);

#endif /* __IMDB_PROC_MGR_H__ */
