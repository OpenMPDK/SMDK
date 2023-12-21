// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020-2021 Intel Corporation. All rights reserved. */
/* Copyright (C) 2005 Andreas Ericsson. All rights reserved. */

/* originally copied from perf and git */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cxl/libcxl.h>
#include <util/parse-options.h>
#include <ccan/array_size/array_size.h>

#include <util/strbuf.h>
#include <util/util.h>
#include <util/main.h>
#include <cxl/builtin.h>

const char cxl_usage_string[] = "cxl [--version] [--help] COMMAND [ARGS]";
const char cxl_more_info_string[] =
	"See 'cxl help COMMAND' for more information on a specific command.\n"
	" cxl --list-cmds to see all available commands";

static int cmd_version(int argc, const char **argv, struct cxl_ctx *ctx)
{
	printf("%s\n", VERSION);
	return 0;
}

static int cmd_help(int argc, const char **argv, struct cxl_ctx *ctx)
{
	const char * const builtin_help_subcommands[] = {
		"list",
		NULL,
	};
	struct option builtin_help_options[] = {
		OPT_END(),
	};
	const char *builtin_help_usage[] = {
		"cxl help [command]",
		NULL
	};

	argc = parse_options_subcommand(argc, argv, builtin_help_options,
			builtin_help_subcommands, builtin_help_usage, 0);

	if (!argv[0]) {
		printf("\n usage: %s\n\n", cxl_usage_string);
		printf("\n %s\n\n", cxl_more_info_string);
		return 0;
	}

	return help_show_man_page(argv[0], "cxl", "CXL_MAN_VIEWER");
}

static struct cmd_struct commands[] = {
	{ "version", .c_fn = cmd_version },
	{ "list", .c_fn = cmd_list },
	{ "help", .c_fn = cmd_help },
	{ "zero-labels", .c_fn = cmd_zero_labels },
	{ "read-labels", .c_fn = cmd_read_labels },
	{ "write-labels", .c_fn = cmd_write_labels },
	{ "disable-memdev", .c_fn = cmd_disable_memdev },
	{ "enable-memdev", .c_fn = cmd_enable_memdev },
	{ "reserve-dpa", .c_fn = cmd_reserve_dpa },
	{ "free-dpa", .c_fn = cmd_free_dpa },
	{ "update-firmware", .c_fn = cmd_update_fw },
	{ "disable-port", .c_fn = cmd_disable_port },
	{ "enable-port", .c_fn = cmd_enable_port },
	{ "set-partition", .c_fn = cmd_set_partition },
	{ "disable-bus", .c_fn = cmd_disable_bus },
	{ "create-region", .c_fn = cmd_create_region },
	{ "enable-region", .c_fn = cmd_enable_region },
	{ "disable-region", .c_fn = cmd_disable_region },
	{ "destroy-region", .c_fn = cmd_destroy_region },
	{ "monitor", .c_fn = cmd_monitor },
#ifdef ENABLE_SMDK_PLUGIN
	{ "inject-poison", .c_fn = cmd_inject_poison },
	{ "clear-poison", .c_fn = cmd_clear_poison },
	{ "set-timestamp", .c_fn = cmd_set_timestamp },
	{ "get-timestamp", .c_fn = cmd_get_timestamp },
	{ "get-event-record", .c_fn = cmd_get_event_record },
	{ "clear-event-record", .c_fn = cmd_clear_event_record },
	{ "get-latency-matrix", .c_fn = cmd_get_latency_matrix },
	{ "disable-cxlswap", .c_fn = cmd_disable_cxlswap },
	{ "enable-cxlswap", .c_fn = cmd_enable_cxlswap },
	{ "check-cxlswap", .c_fn = cmd_check_cxlswap },
	{ "flush-cxlswap", .c_fn = cmd_flush_cxlswap },
	{ "disable-cxlcache", .c_fn = cmd_disable_cxlcache },
	{ "enable-cxlcache", .c_fn = cmd_enable_cxlcache },
	{ "check-cxlcache", .c_fn = cmd_check_cxlcache },
	{ "flush-cxlcache", .c_fn = cmd_flush_cxlcache },
	{ "identify", .c_fn = cmd_identify },
	{ "get-health-info", .c_fn = cmd_get_health_info },
	{ "get-alert-config", .c_fn = cmd_get_alert_config },
	{ "set-alert-config", .c_fn = cmd_set_alert_config },
	{ "get-firmware-info", .c_fn = cmd_get_firmware_info },
	{ "transfer-firmware", .c_fn = cmd_transfer_firmware },
	{ "activate-firmware", .c_fn = cmd_activate_firmware },
	{ "get-shutdown-state", .c_fn = cmd_get_shutdown_state },
	{ "set-shutdown-state", .c_fn = cmd_set_shutdown_state },
	{ "get-scan-media-caps", .c_fn = cmd_get_scan_media_caps },
	{ "scan-media", .c_fn = cmd_scan_media },
	{ "get-scan-media", .c_fn = cmd_get_scan_media },
	{ "sanitize-memdev", .c_fn = cmd_sanitize_memdev },
	{ "get-sld-qos-control", .c_fn = cmd_get_sld_qos_control },
	{ "set-sld-qos-control", .c_fn = cmd_set_sld_qos_control },
	{ "get-sld-qos-status", .c_fn = cmd_get_sld_qos_status },
#endif
};

int main(int argc, const char **argv)
{
	struct cxl_ctx *ctx;
	int rc;

	/* Look for flags.. */
	argv++;
	argc--;
	main_handle_options(&argv, &argc, cxl_usage_string, commands,
			ARRAY_SIZE(commands));

	if (argc > 0) {
		if (!prefixcmp(argv[0], "--"))
			argv[0] += 2;
	} else {
		/* The user didn't specify a command; give them help */
		printf("\n usage: %s\n\n", cxl_usage_string);
		printf("\n %s\n\n", cxl_more_info_string);
		goto out;
	}

	rc = cxl_new(&ctx);
	if (rc)
		goto out;
	main_handle_internal_command(argc, argv, ctx, commands,
			ARRAY_SIZE(commands), PROG_CXL);
	cxl_unref(ctx);
	fprintf(stderr, "Unknown command: '%s'\n", argv[0]);
out:
	return 1;
}
