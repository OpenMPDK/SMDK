// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#include "device_resource.h"
#include "sls.h"
#include <linux/module.h>
#include <linux/pnm/log.h>

static bool use_dax;
module_param(use_dax, bool, 0644);
MODULE_PARM_DESC(use_dax, "Enable DAX for SLS");

// sls initialization is called earlier than this check.
// so we can be sure that if use_dax is set, dax is initialized.
bool sls_is_dax_enabled(void)
{
	return use_dax;
}
EXPORT_SYMBOL(sls_is_dax_enabled);

static int __init init_mod(void)
{
	int err = init_sls_device();

	if (err)
		return err;

	if (use_dax)
		err = init_sls_dax_dev(get_sls_device(), PNM_DAX_MINOR_ID,
				       PNM_DAX_MAJOR_ID); // dax0.0
	else
		PNM_INF("DAX is disabled: use_dax=off\n");

	return err;
}

static void __exit exit_mod(void)
{
	cleanup_sls_device();
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SLS resource manager");

module_init(init_mod);
module_exit(exit_mod);
