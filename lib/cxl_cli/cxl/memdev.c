// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020-2021 Intel Corporation. All rights reserved. */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <util/log.h>
#include <util/json.h>
#include <util/size.h>
#include <cxl/libcxl.h>
#include <util/parse-options.h>
#include <ccan/minmax/minmax.h>
#include <ccan/array_size/array_size.h>

#include "json.h"
#include "filter.h"

struct action_context {
	FILE *f_out;
	FILE *f_in;
	struct json_object *jdevs;
};

static struct parameters {
	const char *outfile;
	const char *infile;
	unsigned len;
	unsigned offset;
	const char *address;
	const char *length;
	bool verbose;
	bool serial;
	bool force;
	bool align;
	const char *type;
	const char *size;
	unsigned event_type;
	bool clear_all;
	unsigned int event_handle;
	const char *decoder_filter;
	const char *event_alert;
	const char *action;
	const char *threshold;
	const char *slot;
	bool online;
} param;

static struct log_ctx ml;

enum cxl_setpart_type {
	CXL_SETPART_PMEM,
	CXL_SETPART_VOLATILE,
};

#define BASE_OPTIONS() \
OPT_BOOLEAN('v',"verbose", &param.verbose, "turn on debug"), \
OPT_BOOLEAN('S', "serial", &param.serial, "use serial numbers to id memdevs")

#define READ_OPTIONS() \
OPT_STRING('o', "output", &param.outfile, "output-file", \
	"filename to write label area contents")

#define WRITE_OPTIONS() \
OPT_STRING('i', "input", &param.infile, "input-file", \
	"filename to read label area data")

#define LABEL_OPTIONS() \
OPT_UINTEGER('s', "size", &param.len, "number of label bytes to operate"), \
OPT_UINTEGER('O', "offset", &param.offset, \
	"offset into the label area to start operation")

#define DISABLE_OPTIONS()                                              \
OPT_BOOLEAN('f', "force", &param.force,                                \
	    "DANGEROUS: override active memdev safety checks")

#define SET_PARTITION_OPTIONS() \
OPT_STRING('t', "type",  &param.type, "type",			\
	"'pmem' or 'ram' (volatile) (Default: 'pmem')"),		\
OPT_STRING('s', "size",  &param.size, "size",			\
	"size in bytes (Default: all available capacity)"),	\
OPT_BOOLEAN('a', "align",  &param.align,			\
	"auto-align --size per device's requirement")

#define RESERVE_DPA_OPTIONS()                                          \
OPT_STRING('s', "size", &param.size, "size",                           \
	   "size in bytes (Default: all available capacity)")

#define DPA_OPTIONS()                                          \
OPT_STRING('d', "decoder", &param.decoder_filter,              \
   "decoder instance id",                                      \
   "override the automatic decoder selection"),                \
OPT_STRING('t', "type", &param.type, "type",                   \
	   "'pmem' or 'ram' (volatile) (Default: 'pmem')"),    \
OPT_BOOLEAN('f', "force", &param.force,                        \
	    "Attempt 'expected to fail' operations")

/************************************ smdk ***********************************/
#define POISON_OPTIONS() \
OPT_STRING('a', "address", &param.address, "dpa",\
	"DPA to inject/clear poison or retrieve poison list from(hex value)")

#define GET_LIST_OPTIONS() \
OPT_STRING('l', "length", &param.length, "addr-length",\
	"range of physical addresses to retrieve the Poison List (hex value)")

#define CLEAR_OPTIONS() \
OPT_STRING('i', "input", &param.infile, "input-file", \
	"filename to read data. Device writes the data while clearing poison")

#define EVENT_OPTIONS() \
OPT_UINTEGER('t', "type", &param.event_type, \
	"type of event, 1: info, 2: warning, 3: failure 4: fatal")

#define EVENT_CLEAR_OPTIONS() \
OPT_BOOLEAN('a', "all", &param.clear_all, "clear all event"), \
OPT_UINTEGER('n', "num_handle", &param.event_handle, \
	"event handle number to clear")

#define SET_ALERT_OPTIONS() \
OPT_STRING('e', "event", &param.event_alert, "event", \
	"event to set warning alert: 'life_used', 'over_temperature', 'under_temperature', 'volatile_mem_error', 'pmem_error'"), \
OPT_STRING('a', "action", &param.action, "en/disable", \
	"enable or disable warning alert"), \
OPT_STRING('t', "threshold", &param.threshold, "threshold", \
	"threshold value to set")

#define TRANSFER_FW_OPTIONS() \
OPT_STRING('i', "input", &param.infile, "input-file", \
	"filename of FW Package to transfer"), \
OPT_STRING('s', "slot", &param.slot, "slot-number", \
	"slot number to transfer FW Package")

#define ACTIVATE_FW_OPTIONS() \
OPT_STRING('s', "slot", &param.slot, "slot-number", \
	"slot number to activate FW"), \
OPT_BOOLEAN('\0', "online", &param.online, \
	"enable online activation (default: activated at next reset)")
/*****************************************************************************/

static const struct option read_options[] = {
	BASE_OPTIONS(),
	LABEL_OPTIONS(),
	READ_OPTIONS(),
	OPT_END(),
};

static const struct option write_options[] = {
	BASE_OPTIONS(),
	LABEL_OPTIONS(),
	WRITE_OPTIONS(),
	OPT_END(),
};

static const struct option zero_options[] = {
	BASE_OPTIONS(),
	LABEL_OPTIONS(),
	OPT_END(),
};

static const struct option disable_options[] = {
	BASE_OPTIONS(),
	DISABLE_OPTIONS(),
	OPT_END(),
};

static const struct option enable_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option set_partition_options[] = {
	BASE_OPTIONS(),
	SET_PARTITION_OPTIONS(),
	OPT_END(),
};

static const struct option reserve_dpa_options[] = {
	BASE_OPTIONS(),
	RESERVE_DPA_OPTIONS(),
	DPA_OPTIONS(),
	OPT_END(),
};

static const struct option free_dpa_options[] = {
	BASE_OPTIONS(),
	DPA_OPTIONS(),
	OPT_END(),
};

/************************************ smdk ***********************************/
static const struct option get_poison_options[] = {
	BASE_OPTIONS(),
	POISON_OPTIONS(),
	GET_LIST_OPTIONS(),
	OPT_END(),
};

static const struct option inject_poison_options[] = {
	BASE_OPTIONS(),
	POISON_OPTIONS(),
	OPT_END(),
};

static const struct option clear_poison_options[] = {
	BASE_OPTIONS(),
	POISON_OPTIONS(),
	CLEAR_OPTIONS(),
	OPT_END(),
};

static const struct option get_timestamp_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option set_timestamp_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option get_event_record_options[] = {
	BASE_OPTIONS(),
	EVENT_OPTIONS(),
	OPT_END(),
};

static const struct option clear_event_record_options[] = {
	BASE_OPTIONS(),
	EVENT_OPTIONS(),
	EVENT_CLEAR_OPTIONS(),
	OPT_END(),
};

static const struct option identify_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option get_health_info_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option get_alert_config_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option set_alert_config_options[] = {
	BASE_OPTIONS(),
	SET_ALERT_OPTIONS(),
	OPT_END(),
};

static const struct option get_firmware_info_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option transfer_firmware_options[] = {
	BASE_OPTIONS(),
	TRANSFER_FW_OPTIONS(),
	OPT_END(),
};

static const struct option activate_firmware_options[] = {
	BASE_OPTIONS(),
	ACTIVATE_FW_OPTIONS(),
	OPT_END(),
};

static int action_get_poison(struct cxl_memdev *memdev,
			     struct action_context *actx)
{
	int rc;
	unsigned long addr = 0;
	unsigned long len = 0;
	if (param.address) {
		addr = (unsigned long)strtol(param.address, NULL, 16);
	}
	if (param.length) {
		len = (unsigned long)strtol(param.length, NULL, 16);
	}

	rc = cxl_memdev_get_poison(memdev, addr, len);
	if (rc < 0) {
		log_err(&ml, "%s: get poison list failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_inject_poison(struct cxl_memdev *memdev,
				struct action_context *actx)
{
	int rc;
	unsigned long addr;
	if (!param.address) {
		log_err(&ml, "invalid address, aborting\n");
		return -EINVAL;
	} else
		addr = (unsigned long)strtol(param.address, NULL, 16);

	rc = cxl_memdev_inject_poison(memdev, addr);
	if (rc < 0) {
		log_err(&ml, "%s: inject poison failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_clear_poison(struct cxl_memdev *memdev,
			       struct action_context *actx)
{
#define cxl_memdev_clear_poison_data_size 64
	size_t size, read_len;
	unsigned char *buf;
	int rc;
	unsigned long addr;

	if (!param.address) {
		log_err(&ml, "invalid address, aborting\n");
		return -EINVAL;
	} else
		addr = (unsigned long)strtol(param.address, NULL, 16);

	buf = calloc(1, cxl_memdev_clear_poison_data_size);
	if (!buf)
		return -ENOMEM;

	if (actx->f_in != stdin) {
		fseek(actx->f_in, 0L, SEEK_END);
		size = (size_t)ftell(actx->f_in);
		fseek(actx->f_in, 0L, SEEK_SET);

		if (size > cxl_memdev_clear_poison_data_size) {
			log_err(&ml,
				"File size (%zu) greater than input payload size(%u), aborting\n",
				size, cxl_memdev_clear_poison_data_size);
			free(buf);
			return -EINVAL;
		}
		read_len = fread(buf, 1, size, actx->f_in);
		if (read_len != size) {
			rc = -ENXIO;
			goto out;
		}
	}

	rc = cxl_memdev_clear_poison(memdev, buf, addr);
	if (rc < 0)
		log_err(&ml, "%s: clear poison failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));

out:
	free(buf);
	return rc;
}

static int action_get_timestamp(struct cxl_memdev *memdev,
				struct action_context *actx)
{
	int rc;
	rc = cxl_memdev_get_timestamp(memdev);
	if (rc < 0) {
		log_err(&ml, "%s: get timestamp failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_set_timestamp(struct cxl_memdev *memdev,
				struct action_context *actx)
{
	int rc;
	time_t t = time(NULL) * nano_scale;
	rc = cxl_memdev_set_timestamp(memdev, t);
	if (rc < 0) {
		log_err(&ml, "%s: set timestamp failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_get_event_record(struct cxl_memdev *memdev,
				   struct action_context *actx)
{
	int rc;

	if (!param.event_type || param.event_type > 4) {
		log_err(&ml, "Invalid event type. aborting\n");
		return -EINVAL;
	}
	rc = cxl_memdev_get_event_record(memdev, param.event_type - 1);
	if (rc < 0) {
		log_err(&ml, "%s: read event record failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_clear_event_record(struct cxl_memdev *memdev,
				     struct action_context *actx)
{
	int rc;

	if (!param.event_type || param.event_type > 4) {
		log_err(&ml, "Invalid event type. aborting\n");
		return -EINVAL;
	}
	if (param.event_handle == 0 && !param.clear_all) {
		log_err(&ml, "Designate handle or use -a to clear all.\n");
		return -EINVAL;
	}
	rc = cxl_memdev_clear_event_record(memdev, param.event_type - 1,
					   param.clear_all, param.event_handle);
	if (rc < 0) {
		log_err(&ml, "%s: clear event record failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static void bytes_to_str(unsigned long long bytes, char *str, int len)
{
	/* convert bytes to human friendly formats (B, KB, MB, GB, TB) */

	if (bytes == ULLONG_MAX)
		snprintf(str, len, "Invalid");
	else if (bytes < SZ_1K)
		snprintf(str, len, "%lld B", bytes);
	else if (bytes < SZ_1M)
		snprintf(str, len, "%.2lf KB", (double)bytes / SZ_1K);
	else if (bytes < SZ_1G)
		snprintf(str, len, "%.2lf MB", (double)bytes / SZ_1M);
	else if (bytes < SZ_1T)
		snprintf(str, len, "%.2lf GB", (double)bytes / SZ_1G);
	else
		snprintf(str, len, "%.2lf TB", (double)bytes / SZ_1T);
}

static int action_identify(struct cxl_memdev *memdev,
			   struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	int rc;
	char fw_rev[0x10], total_cap[10], volatile_only[10],
		persistent_only[10], alignment[10], lsa_size[10];
	int info_log_size, warn_log_size, fail_log_size, fatal_log_size,
		poison_list_max, inject_poison_limit, inject_persistent_poison,
		scan_poison, egress_port_congestion, temp_throughput_reduction;

	cmd = cxl_cmd_new_identify(memdev);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
		goto out;
	}

	rc = cxl_cmd_identify_get_fw_rev(cmd, fw_rev, 0x10);
	if (rc != 0) {
		log_err(&ml, "%s: can't get firmware revision\n", devname);
		goto out;
	}

	bytes_to_str(cxl_cmd_identify_get_total_size(cmd), total_cap,
		     sizeof(total_cap));
	bytes_to_str(cxl_cmd_identify_get_volatile_only_size(cmd),
		     volatile_only, sizeof(volatile_only));
	bytes_to_str(cxl_cmd_identify_get_persistent_only_size(cmd),
		     persistent_only, sizeof(persistent_only));
	bytes_to_str(cxl_cmd_identify_get_partition_align(cmd), alignment,
		     sizeof(alignment));
	info_log_size =
		cxl_cmd_identify_get_event_log_size(cmd, CXL_IDENTIFY_INFO);
	warn_log_size =
		cxl_cmd_identify_get_event_log_size(cmd, CXL_IDENTIFY_WARN);
	fail_log_size =
		cxl_cmd_identify_get_event_log_size(cmd, CXL_IDENTIFY_FAIL);
	fatal_log_size =
		cxl_cmd_identify_get_event_log_size(cmd, CXL_IDENTIFY_FATAL);
	bytes_to_str(cxl_cmd_identify_get_label_size(cmd), lsa_size,
		     sizeof(lsa_size));
	poison_list_max = cxl_cmd_identify_get_poison_list_max(cmd);
	inject_poison_limit = cxl_cmd_identify_get_inject_poison_limit(cmd);
	inject_persistent_poison =
		cxl_cmd_identify_injects_persistent_poison(cmd);
	scan_poison = cxl_cmd_identify_scans_for_poison(cmd);
	egress_port_congestion = cxl_cmd_identify_egress_port_congestion(cmd);
	temp_throughput_reduction =
		cxl_cmd_identify_temporary_throughput_reduction(cmd);

	printf("CXL Identify Memory Device \"%s\"\n", devname);
	printf("FW Revision                              : %s\n", fw_rev);
	printf("Total Capacity                           : %s\n", total_cap);
	printf("Volatile Only Capacity                   : %s\n",
	       volatile_only);
	printf("Persistent Only Capacity                 : %s\n",
	       persistent_only);
	printf("Partition Alignment                      : %s\n", alignment);
	printf("Informational Event Log Size             : %d\n",
	       info_log_size);
	printf("Warning Event Log Size                   : %d\n",
	       warn_log_size);
	printf("Failure Event Log Size                   : %d\n",
	       fail_log_size);
	printf("Fatal Event Log Size                     : %d\n",
	       fatal_log_size);
	printf("LSA Size                                 : %s\n", lsa_size);
	printf("Poison List Maximum Media Error Records  : %d\n",
	       poison_list_max);
	printf("Inject Poison Limit                      : %d\n",
	       inject_poison_limit);
	printf("Poison Handling Capabilities\n");
	printf("Injects Persistent Poison                : ");
	if (inject_persistent_poison)
		printf("Supported\n");
	else
		printf("Not Supported\n");
	printf("Scans for Poison                         : ");
	if (scan_poison)
		printf("Supported\n");
	else
		printf("Not Supported\n");
	printf("QoS Telemetry Capabilities\n");
	printf("Egress Port Congestion                   : ");
	if (egress_port_congestion)
		printf("Supported\n");
	else
		printf("Not Supported\n");
	printf("Temporary Throughput Reduction           : ");
	if (temp_throughput_reduction)
		printf("Supported\n");
	else
		printf("Not Supported\n");

out:
	cxl_cmd_unref(cmd);
	return rc;
}

static int action_get_health_info(struct cxl_memdev *memdev,
				  struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	int rc;
	int life_used, temperature, volatile_err, persistent_err;

	cmd = cxl_cmd_new_get_health_info(memdev);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
		goto out;
	}

	printf("CXL Get Health Information Memory Device \"%s\"\n", devname);

	printf("Health Status                    : ");
	if (cxl_cmd_health_info_get_hw_replacement_needed(cmd))
		printf("Hardware Replacemenet Needed\n");
	else if (cxl_cmd_health_info_get_performance_degraded(cmd))
		printf("Performance Degraded\n");
	else if (cxl_cmd_health_info_get_maintenance_needed(cmd))
		printf("Maintenance Needed\n");
	else
		printf("Normal\n");

	printf("Media Status                     : ");
	if (cxl_cmd_health_info_get_media_normal(cmd))
		printf("Normal\n");
	else if (cxl_cmd_health_info_get_media_not_ready(cmd))
		printf("Not Ready\n");
	else if (cxl_cmd_health_info_get_media_persistence_lost(cmd))
		printf("Write Persistency Lost\n");
	else if (cxl_cmd_health_info_get_media_powerloss_persistence_loss(cmd))
		printf("Write Persistency Lost by Power Loss\n");
	else if (cxl_cmd_health_info_get_media_shutdown_persistence_loss(cmd))
		printf("Write Persistency Lost by Shutdown\n");
	else if (cxl_cmd_health_info_get_media_data_lost(cmd))
		printf("All Data Lost\n");
	else if (cxl_cmd_health_info_get_media_powerloss_data_loss(cmd))
		printf("All Data Lost by Power Loss\n");
	else if (cxl_cmd_health_info_get_media_shutdown_data_loss(cmd))
		printf("All Data Lost by Shutdown\n");
	else if (cxl_cmd_health_info_get_media_persistence_loss_imminent(cmd))
		printf("Write Persistency Loss Imminent\n");
	else if (cxl_cmd_health_info_get_media_data_loss_imminent(cmd))
		printf("All Data Lost Imminent\n");

	life_used = cxl_cmd_health_info_get_life_used(cmd);
	printf("Life Used                        : ");
	if (life_used == -EOPNOTSUPP)
		printf("Not Implemented\n");
	else {
		printf("%d %% ", life_used);
		if (cxl_cmd_health_info_get_ext_life_used_normal(cmd))
			printf("(Normal)\n");
		else if (cxl_cmd_health_info_get_ext_life_used_warning(cmd))
			printf("(Warning)\n");
		else if (cxl_cmd_health_info_get_ext_life_used_critical(cmd))
			printf("(Critical)\n");
		else
			printf("(Unknown)\n");
	}

	temperature = cxl_cmd_health_info_get_temperature(cmd);
	printf("Device Temperature               : ");
	if (temperature == -EOPNOTSUPP)
		printf("Not Implemented\n");
	else {
		printf("%hd C ", temperature);
		if (cxl_cmd_health_info_get_ext_temperature_normal(cmd))
			printf("(Normal)\n");
		else if (cxl_cmd_health_info_get_ext_temperature_warning(cmd))
			printf("(Warning)\n");
		else if (cxl_cmd_health_info_get_ext_temperature_critical(cmd))
			printf("(Critical)\n");
		else
			printf("(Unknown)\n");
	}

	volatile_err = cxl_cmd_health_info_get_volatile_errors(cmd);
	printf("Corrected Volatile Error Count   : %u ", volatile_err);
	if (cxl_cmd_health_info_get_ext_corrected_volatile_normal(cmd))
		printf("(Normal)\n");
	else if (cxl_cmd_health_info_get_ext_corrected_volatile_warning(cmd))
		printf("(Warning)\n");
	else
		printf("(Unknown)\n");

	persistent_err = cxl_cmd_health_info_get_pmem_errors(cmd);
	printf("Corrected Persistent Error Count : %u ", persistent_err);
	if (cxl_cmd_health_info_get_ext_corrected_persistent_normal(cmd))
		printf("(Normal)\n");
	else if (cxl_cmd_health_info_get_ext_corrected_persistent_warning(cmd))
		printf("(Warning)\n");
	else
		printf("(Unknown)\n");

	printf("Dirty Shutdown Count             : %u\n",
	       cxl_cmd_health_info_get_dirty_shutdowns(cmd));

out:
	cxl_cmd_unref(cmd);
	return rc;
}

static int action_get_alert_config(struct cxl_memdev *memdev,
				   struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	int rc;

	cmd = cxl_cmd_new_get_alert_config(memdev);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
		goto out;
	}

	printf("CXL Get Alert Configuration Memory Device \"%s\"\n", devname);

	printf("Life Used Threshold - Critical                        : %d %%\n",
	       cxl_cmd_alert_config_get_life_used_crit_alert_threshold(cmd));
	printf("                    - Warning                         : ");
	if (!cxl_cmd_alert_config_life_used_prog_warn_threshold_writable(cmd))
		printf("Not Supported\n");
	else if (!cxl_cmd_alert_config_life_used_prog_warn_threshold_valid(cmd))
		printf("Not Set\n");
	else
		printf("%d %%\n",
		       cxl_cmd_alert_config_get_life_used_prog_warn_threshold(
			       cmd));

	printf("Device Over-Temperature Threshold - Critical          : %hd C\n",
	       cxl_cmd_alert_config_get_dev_over_temperature_crit_alert_threshold(
		       cmd));
	printf("                                  - Warning           : ");
	if (!cxl_cmd_alert_config_dev_over_temperature_prog_warn_threshold_writable(
		    cmd))
		printf("Not Supported\n");
	else if (!cxl_cmd_alert_config_dev_over_temperature_prog_warn_threshold_valid(
			 cmd))
		printf("Not Set\n");
	else
		printf("%hd C\n",
		       cxl_cmd_alert_config_get_dev_over_temperature_prog_warn_threshold(
			       cmd));

	printf("Device Under-Temperature Threshold - Critical         : %hd C\n",
	       cxl_cmd_alert_config_get_dev_under_temperature_crit_alert_threshold(
		       cmd));
	printf("                                   - Warning          : ");
	if (!cxl_cmd_alert_config_dev_under_temperature_prog_warn_threshold_writable(
		    cmd))
		printf("Not Supported\n");
	else if (!cxl_cmd_alert_config_dev_under_temperature_prog_warn_threshold_valid(
			 cmd))
		printf("Not Set\n");
	else
		printf("%hd C\n",
		       cxl_cmd_alert_config_get_dev_under_temperature_prog_warn_threshold(
			       cmd));

	printf("Corrected Volatile Memory Error Threshold - Warning   : ");
	if (!cxl_cmd_alert_config_corrected_volatile_mem_err_prog_warn_threshold_writable(
		    cmd))
		printf("Not Supported\n");
	else if (!cxl_cmd_alert_config_corrected_volatile_mem_err_prog_warn_threshold_valid(
			 cmd))
		printf("Not Set\n");
	else
		printf("%d\n",
		       cxl_cmd_alert_config_get_corrected_volatile_mem_err_prog_warn_threshold(
			       cmd));

	printf("Corrected Persistent Memory Error Threshold - Warning : ");
	if (!cxl_cmd_alert_config_corrected_pmem_err_prog_warn_threshold_writable(
		    cmd))
		printf("Not Supported\n");
	else if (!cxl_cmd_alert_config_corrected_pmem_err_prog_warn_threshold_valid(
			 cmd))
		printf("Not Set\n");
	else
		printf("%d\n",
		       cxl_cmd_alert_config_get_corrected_pmem_err_prog_warn_threshold(
			       cmd));

out:
	cxl_cmd_unref(cmd);
	return rc;
}

static int validate_alert_threshold(enum cxl_setalert_event event,
				    int threshold)
{
	if (event == CXL_SETALERT_LIFE) {
		if (threshold < 0 || threshold > 100) {
			log_err(&ml, "life_used threshold: (0 - 100)\n");
			return -EINVAL;
		}
	} else if (event == CXL_SETALERT_OVER_TEMP ||
		   event == CXL_SETALERT_UNDER_TEMP) {
		if (threshold < SHRT_MIN || threshold > SHRT_MAX) {
			log_err(&ml,
				"temperature threshold: (-32,768 - 32,767)\n");
			return -EINVAL;
		}
	} else {
		if (threshold < 0 || threshold > USHRT_MAX) {
			log_err(&ml, "error count threshold: (0 - 65,535)\n");
			return -EINVAL;
		}
	}
	return 0;
}

static int action_get_firmware_info(struct cxl_memdev *memdev,
				    struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	int rc, slots_supported, active_slot, staged_slot, online_activation;
	char fw_revs[4][0x10];

	cmd = cxl_cmd_new_get_firmware_info(memdev);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
		goto out;
	}

	slots_supported = cxl_cmd_firmware_info_get_slots_supported(cmd);
	active_slot = cxl_cmd_firmware_info_get_active_slot(cmd);
	staged_slot = cxl_cmd_firmware_info_get_staged_slot(cmd);
	online_activation =
		cxl_cmd_firmware_info_online_activation_capable(cmd);
	for (int i = 1; i <= slots_supported; i++) {
		rc = cxl_cmd_firmware_info_get_fw_rev(cmd, fw_revs[i - 1], 0x10,
						      i);
		if (rc != 0) {
			log_err(&ml, "%s: can't get firmware revision\n", devname);
			goto out;
		}
	}

	printf("Supported FW Slots           : %d\n", slots_supported);
	printf("Active FW Slot               : %d\n", active_slot);
	printf("Staged FW Slot               : %d\n", staged_slot);
	printf("Online Activation Capability : %s\n",
	       online_activation ? "Supported" : "Not Supported");
	for (int i = 1; i <= slots_supported; i++)
		printf("Slot %d FW revision           : %s\n", i,
		       fw_revs[i - 1]);

out:
	cxl_cmd_unref(cmd);
	return rc;
}

static int action_transfer_firmware(struct cxl_memdev *memdev,
				    struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	int rc, slot, slots_supported, active_slot;
	unsigned int fw_size, remain, transfer_len, transfer_len_max,
		offset = 0;
	unsigned char *buf = NULL;
	size_t read_len;
	enum cxl_transfer_fw_action action;

	if (!param.infile) {
		log_err(&ml, "FW Package file is not selected\n");
		return -EINVAL;
	}

	if (!param.slot) {
		log_err(&ml, "Slot to transfer FW Package is not specified\n");
		return -EINVAL;
	}
	slot = atoi(param.slot);

	cmd = cxl_cmd_new_get_firmware_info(memdev);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out_cmd;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
		goto out_cmd;
	}

	slots_supported = cxl_cmd_firmware_info_get_slots_supported(cmd);
	active_slot = cxl_cmd_firmware_info_get_active_slot(cmd);

	cxl_cmd_unref(cmd);

	if (slot <= 0 || slot > slots_supported) {
		log_err(&ml,
			"%s: invalid slot %d, device supports %d FW slots\n",
			devname, slot, slots_supported);
		return -EINVAL;
	} else if (slot == active_slot) {
		log_err(&ml, "%s: slot %d is active slot\n", devname, slot);
		return -EINVAL;
	}

	fseek(actx->f_in, 0L, SEEK_END);
	fw_size = ftell(actx->f_in);
	fseek(actx->f_in, 0L, SEEK_SET);

	if (fw_size % 0x80) {
		log_err(&ml, "FW Package size should be 128 bytes aligned\n");
		return -EINVAL;
	}

	remain = fw_size;
	offset = 0;
	transfer_len_max =
		ALIGN_DOWN(cxl_memdev_get_payload_max(memdev), 0x80) - 0x80;

	while (remain > 0) {
		transfer_len = min(remain, transfer_len_max);
		buf = calloc(1, transfer_len);
		if (!buf) {
			rc = -ENOMEM;
			goto out;
		}

		read_len = fread(buf, 1, transfer_len, actx->f_in);
		if (read_len != transfer_len) {
			rc = -ENXIO;
			goto out;
		}

		if (offset == 0) {
			if (transfer_len == fw_size)
				action = CXL_TRANSFER_FW_FULL;
			else
				action = CXL_TRANSFER_FW_INIT;
		} else if (transfer_len == remain)
			action = CXL_TRANSFER_FW_END;
		else
			action = CXL_TRANSFER_FW_CONT;

		cmd = cxl_cmd_new_transfer_firmware(memdev, action, slot,
						    offset, buf, transfer_len);
		if (!cmd) {
			rc = -ENOMEM;
			goto out;
		}

		rc = cxl_cmd_submit(cmd);
		if (rc < 0) {
			log_err(&ml, "cmd submission failed: %s\n",
				strerror(-rc));
			goto out_cmd;
		}

		rc = cxl_cmd_get_mbox_status(cmd);
		if (rc != 0) {
			log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
			rc = -ENXIO;
			goto out_cmd;
		}

		remain -= transfer_len;
		offset += transfer_len / 0x80;
		cxl_cmd_unref(cmd);
		free(buf);
	}

	return rc;

out_cmd:
	cxl_cmd_unref(cmd);
out:
	if (buf)
		free(buf);
	if (offset > 0) {
		int abort_rc;

		log_err(&ml, "error occurs during transferring FW, send abort...");
		cmd = cxl_cmd_new_transfer_firmware(
			memdev, CXL_TRANSFER_FW_ABORT, 0, 0, NULL, 0);
		if (!cmd) {
			log_err(&ml, "aborting transfer FW failed: %s\n",
				strerror(ENOMEM));
			return rc;
		}

		abort_rc = cxl_cmd_submit(cmd);
		if (abort_rc < 0) {
			log_err(&ml, "cmd submission failed: %s\n",
				strerror(-abort_rc));
			log_err(&ml, "aborting transfer FW failed: %s\n",
				strerror(-abort_rc));
			cxl_cmd_unref(cmd);
			return rc;
		}

		abort_rc = cxl_cmd_get_mbox_status(cmd);
		if (abort_rc != 0) {
			log_err(&ml, "%s: mbox status: %d\n", __func__, abort_rc);
			log_err(&ml, "aborting transfer FW failed: %s\n",
				strerror(ENXIO));
		}

		cxl_cmd_unref(cmd);
	}

	return rc;
}

static int action_activate_firmware(struct cxl_memdev *memdev,
				    struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	int rc, slot, slots_supported, active_slot, online_activation_capable;
	bool online;

	if (!param.slot) {
		log_err(&ml, "Slot to activate FW is not specified\n");
		return -EINVAL;
	}
	slot = atoi(param.slot);

	cmd = cxl_cmd_new_get_firmware_info(memdev);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
		goto out;
	}

	slots_supported = cxl_cmd_firmware_info_get_slots_supported(cmd);
	active_slot = cxl_cmd_firmware_info_get_active_slot(cmd);
	online_activation_capable =
		cxl_cmd_firmware_info_online_activation_capable(cmd);

	cxl_cmd_unref(cmd);

	if (slot <= 0 || slot > slots_supported) {
		log_err(&ml,
			"%s: invalid slot %d, device supports %d FW slots\n",
			devname, slot, slots_supported);
		return -EINVAL;
	} else if (slot == active_slot) {
		log_err(&ml, "%s: slot %d is already active\n", devname, slot);
		return -EINVAL;
	}

	online = param.online;
	if (param.online && !online_activation_capable) {
		log_err(&ml, "device does not support online activation");
		log_err(&ml, "FW will be activated on the next cold reset");
		online = false;
	}

	cmd = cxl_cmd_new_activate_firmware(memdev, online, slot);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
	}

out:
	cxl_cmd_unref(cmd);
	return rc;
}

static int action_set_alert_config(struct cxl_memdev *memdev,
				   struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	enum cxl_setalert_event event;
	int rc, enable, threshold;

	if (!param.event_alert) {
		log_err(&ml, "%s: event is not specified\n", devname);
		return -EINVAL;
	}

	if (!param.action) {
		log_err(&ml, "%s: action is not specified\n", devname);
		return -EINVAL;
	}

	if (strcmp(param.event_alert, "life_used") == 0)
		event = CXL_SETALERT_LIFE;
	else if (strcmp(param.event_alert, "over_temperature") == 0)
		event = CXL_SETALERT_OVER_TEMP;
	else if (strcmp(param.event_alert, "under_temperature") == 0)
		event = CXL_SETALERT_UNDER_TEMP;
	else if (strcmp(param.event_alert, "volatile_mem_error") == 0)
		event = CXL_SETALERT_VOLATILE_ERROR;
	else if (strcmp(param.event_alert, "pmem_error") == 0)
		event = CXL_SETALERT_PMEM_ERROR;
	else {
		log_err(&ml, "%s: invalid event: %s\n", devname, param.event_alert);
		return -EINVAL;
	}

	if (strcmp(param.action, "enable") == 0) {
		if (!param.threshold) {
			log_err(&ml, "%s: threshold is not specified\n",
				devname);
			return -EINVAL;
		}
		enable = 1;
		threshold = atoi(param.threshold);
		rc = validate_alert_threshold(event, threshold);
		if (rc < 0) {
			log_err(&ml, "%s: invalid threshold: %d\n", devname,
				threshold);
			return rc;
		}
	} else if (strcmp(param.action, "disable") == 0) {
		enable = 0;
		threshold = 0;
	} else {
		log_err(&ml, "%s: invalid action: %s\n", devname, param.action);
		return -EINVAL;
	}

	cmd = cxl_cmd_new_set_alert_config(memdev, event, enable, threshold);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
		goto out;
	}

	if (enable)
		printf("%s warning alert enabled to %d\n", param.event_alert,
		       threshold);
	else
		printf("%s warning alert disabled\n", param.event_alert);

out:
	cxl_cmd_unref(cmd);
	return rc;
}
/*****************************************************************************/

enum reserve_dpa_mode {
	DPA_ALLOC,
	DPA_FREE,
};

static int __reserve_dpa(struct cxl_memdev *memdev,
			 enum reserve_dpa_mode alloc_mode,
			 struct action_context *actx)
{
	struct cxl_decoder *decoder, *auto_target = NULL, *target = NULL;
	struct cxl_endpoint *endpoint = cxl_memdev_get_endpoint(memdev);
	const char *devname = cxl_memdev_get_devname(memdev);
	unsigned long long avail_dpa, size;
	enum cxl_decoder_mode mode;
	struct cxl_port *port;
	char buf[256];
	int rc;

	if (param.type) {
		mode = cxl_decoder_mode_from_ident(param.type);
		if (mode == CXL_DECODER_MODE_NONE) {
			log_err(&ml, "%s: unsupported type: %s\n", devname,
				param.type);
			return -EINVAL;
		}
	} else
		mode = CXL_DECODER_MODE_RAM;

	if (!endpoint) {
		log_err(&ml, "%s: CXL operation disabled\n", devname);
		return -ENXIO;
	}

	port = cxl_endpoint_get_port(endpoint);

	if (mode == CXL_DECODER_MODE_RAM)
		avail_dpa = cxl_memdev_get_ram_size(memdev);
	else
		avail_dpa = cxl_memdev_get_pmem_size(memdev);

	cxl_decoder_foreach(port, decoder) {
		size = cxl_decoder_get_dpa_size(decoder);
		if (size == ULLONG_MAX)
			continue;
		if (cxl_decoder_get_mode(decoder) != mode)
			continue;

		if (size > avail_dpa) {
			log_err(&ml, "%s: capacity accounting error\n",
				devname);
			return -ENXIO;
		}
		avail_dpa -= size;
	}

	if (!param.size)
		if (alloc_mode == DPA_ALLOC) {
			size = avail_dpa;
			if (!avail_dpa) {
				log_err(&ml, "%s: no available capacity\n",
					devname);
				return -ENOSPC;
			}
		} else
			size = 0;
	else {
		size = parse_size64(param.size);
		if (size == ULLONG_MAX) {
			log_err(&ml, "%s: failed to parse size option '%s'\n",
				devname, param.size);
			return -EINVAL;
		}
		if (size > avail_dpa) {
			log_err(&ml, "%s: '%s' exceeds available capacity\n",
				devname, param.size);
			if (!param.force)
				return -ENOSPC;
		}
	}

	/*
	 * Find next free decoder, assumes cxl_decoder_foreach() is in
	 * hardware instance-id order
	 */
	if (alloc_mode == DPA_ALLOC)
		cxl_decoder_foreach(port, decoder) {
			/* first 0-dpa_size is our target */
			if (cxl_decoder_get_dpa_size(decoder) == 0) {
				auto_target = decoder;
				break;
			}
		}
	else
		cxl_decoder_foreach_reverse(port, decoder) {
			/* nothing to free? */
			if (!cxl_decoder_get_dpa_size(decoder))
				continue;
			/*
			 * Active decoders can't be freed, and by definition all
			 * previous decoders must also be active
			 */
			if (cxl_decoder_get_size(decoder))
				break;
			/* first dpa_size > 0 + disabled decoder is our target */
			if (cxl_decoder_get_dpa_size(decoder) < ULLONG_MAX) {
				auto_target = decoder;
				break;
			}
		}

	if (param.decoder_filter) {
		unsigned long id;
		char *end;

		id = strtoul(param.decoder_filter, &end, 0);
		/* allow for standalone ordinal decoder ids */
		if (*end == '\0')
			rc = snprintf(buf, sizeof(buf), "decoder%d.%ld",
				      cxl_port_get_id(port), id);
		else
			rc = snprintf(buf, sizeof(buf), "%s",
				      param.decoder_filter);

		if (rc >= (int)sizeof(buf)) {
			log_err(&ml, "%s: decoder filter '%s' too long\n",
				devname, param.decoder_filter);
			return -EINVAL;
		}

		if (alloc_mode == DPA_ALLOC)
			cxl_decoder_foreach(port, decoder) {
				target = util_cxl_decoder_filter(decoder, buf);
				if (target)
					break;
			}
		else
			cxl_decoder_foreach_reverse(port, decoder) {
				target = util_cxl_decoder_filter(decoder, buf);
				if (target)
					break;
			}

		if (!target) {
			log_err(&ml, "%s: no match for decoder: '%s'\n",
				devname, param.decoder_filter);
			return -ENXIO;
		}

		if (target != auto_target) {
			log_err(&ml, "%s: %s is out of sequence\n", devname,
				cxl_decoder_get_devname(target));
			if (!param.force)
				return -EINVAL;
		}
	}

	if (!target)
		target = auto_target;

	if (!target) {
		log_err(&ml, "%s: no suitable decoder found\n", devname);
		return -ENXIO;
	}

	if (cxl_decoder_get_mode(target) != mode) {
		rc = cxl_decoder_set_dpa_size(target, 0);
		if (rc) {
			log_err(&ml,
				"%s: %s: failed to clear allocation to set mode\n",
				devname, cxl_decoder_get_devname(target));
			return rc;
		}
		rc = cxl_decoder_set_mode(target, mode);
		if (rc) {
			log_err(&ml, "%s: %s: failed to set %s mode\n", devname,
				cxl_decoder_get_devname(target),
				mode == CXL_DECODER_MODE_PMEM ? "pmem" : "ram");
			return rc;
		}
	}

	rc = cxl_decoder_set_dpa_size(target, size);
	if (rc)
		log_err(&ml, "%s: %s: failed to set dpa allocation\n", devname,
			cxl_decoder_get_devname(target));
	else {
		struct json_object *jdev, *jdecoder;
		unsigned long flags = 0;

		if (actx->f_out == stdout && isatty(1))
			flags |= UTIL_JSON_HUMAN;
		jdev = util_cxl_memdev_to_json(memdev, flags);
		jdecoder = util_cxl_decoder_to_json(target, flags);
		if (!jdev || !jdecoder) {
			json_object_put(jdev);
			json_object_put(jdecoder);
		} else {
			json_object_object_add(jdev, "decoder", jdecoder);
			json_object_array_add(actx->jdevs, jdev);
		}
	}
	return rc;
}

static int action_reserve_dpa(struct cxl_memdev *memdev,
			      struct action_context *actx)
{
	return __reserve_dpa(memdev, DPA_ALLOC, actx);
}

static int action_free_dpa(struct cxl_memdev *memdev,
			   struct action_context *actx)
{
	return __reserve_dpa(memdev, DPA_FREE, actx);
}

static int action_disable(struct cxl_memdev *memdev, struct action_context *actx)
{
	if (!cxl_memdev_is_enabled(memdev))
		return 0;

	if (!param.force) {
		/* TODO: actually detect rather than assume active */
		log_err(&ml, "%s is part of an active region\n",
			cxl_memdev_get_devname(memdev));
		return -EBUSY;
	}

	return cxl_memdev_disable_invalidate(memdev);
}

static int action_enable(struct cxl_memdev *memdev, struct action_context *actx)
{
	return cxl_memdev_enable(memdev);
}

static int action_zero(struct cxl_memdev *memdev, struct action_context *actx)
{
	size_t size;
	int rc;

	if (param.len)
		size = param.len;
	else
		size = cxl_memdev_get_label_size(memdev);

	if (cxl_memdev_nvdimm_bridge_active(memdev)) {
		log_err(&ml,
			"%s: has active nvdimm bridge, abort label write\n",
			cxl_memdev_get_devname(memdev));
		return -EBUSY;
	}

	rc = cxl_memdev_zero_label(memdev, size, param.offset);
	if (rc < 0)
		log_err(&ml, "%s: label zeroing failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));

	return rc;
}

static int action_write(struct cxl_memdev *memdev, struct action_context *actx)
{
	size_t size = param.len, read_len;
	unsigned char *buf;
	int rc;

	if (cxl_memdev_nvdimm_bridge_active(memdev)) {
		log_err(&ml,
			"%s: has active nvdimm bridge, abort label write\n",
			cxl_memdev_get_devname(memdev));
		return -EBUSY;
	}

	if (!size) {
		size_t label_size = cxl_memdev_get_label_size(memdev);

		fseek(actx->f_in, 0L, SEEK_END);
		size = ftell(actx->f_in);
		fseek(actx->f_in, 0L, SEEK_SET);

		if (size > label_size) {
			log_err(&ml,
				"File size (%zu) greater than label area size (%zu), aborting\n",
				size, label_size);
			return -EINVAL;
		}
	}

	buf = calloc(1, size);
	if (!buf)
		return -ENOMEM;

	read_len = fread(buf, 1, size, actx->f_in);
	if (read_len != size) {
		rc = -ENXIO;
		goto out;
	}

	rc = cxl_memdev_write_label(memdev, buf, size, param.offset);
	if (rc < 0)
		log_err(&ml, "%s: label write failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));

out:
	free(buf);
	return rc;
}

static int action_read(struct cxl_memdev *memdev, struct action_context *actx)
{
	size_t size, write_len;
	char *buf;
	int rc;

	if (param.len)
		size = param.len;
	else
		size = cxl_memdev_get_label_size(memdev);

	buf = calloc(1, size);
	if (!buf)
		return -ENOMEM;

	rc = cxl_memdev_read_label(memdev, buf, size, param.offset);
	if (rc < 0) {
		log_err(&ml, "%s: label read failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
		goto out;
	}

	write_len = fwrite(buf, 1, size, actx->f_out);
	if (write_len != size) {
		rc = -ENXIO;
		goto out;
	}
	fflush(actx->f_out);

out:
	free(buf);
	return rc;
}

static unsigned long long
partition_align(const char *devname, enum cxl_setpart_type type,
		unsigned long long volatile_size, unsigned long long alignment,
		unsigned long long available)
{
	if (IS_ALIGNED(volatile_size, alignment))
		return volatile_size;

	if (!param.align) {
		log_err(&ml, "%s: size %lld is not partition aligned %lld\n",
			devname, volatile_size, alignment);
		return ULLONG_MAX;
	}

	/* Align based on partition type to fulfill users size request */
	if (type == CXL_SETPART_PMEM)
		volatile_size = ALIGN_DOWN(volatile_size, alignment);
	else
		volatile_size = ALIGN(volatile_size, alignment);

	/* Fail if the align pushes size over the available limit. */
	if (volatile_size > available) {
		log_err(&ml, "%s: aligned partition size %lld exceeds available size %lld\n",
			devname, volatile_size, available);
		volatile_size = ULLONG_MAX;
	}

	return volatile_size;
}

static unsigned long long
param_size_to_volatile_size(const char *devname, enum cxl_setpart_type type,
		unsigned long long size, unsigned long long available)
{
	/* User omits size option. Apply all available capacity to type. */
	if (size == ULLONG_MAX) {
		if (type == CXL_SETPART_PMEM)
			return 0;
		return available;
	}

	/* User includes a size option. Apply it to type */
	if (size > available) {
		log_err(&ml, "%s: %lld exceeds available capacity %lld\n",
			devname, size, available);
			return ULLONG_MAX;
	}
	if (type == CXL_SETPART_PMEM)
		return available - size;
	return size;
}

/*
 * Return the volatile_size to use in the CXL set paritition
 * command, or ULLONG_MAX if unable to validate the partition
 * request.
 */
static unsigned long long
validate_partition(struct cxl_memdev *memdev, enum cxl_setpart_type type,
		unsigned long long size)
{
	unsigned long long total_cap, volatile_only, persistent_only;
	const char *devname = cxl_memdev_get_devname(memdev);
	unsigned long long volatile_size = ULLONG_MAX;
	unsigned long long available, alignment;
	struct cxl_cmd *cmd;
	int rc;

	cmd = cxl_cmd_new_identify(memdev);
	if (!cmd)
		return ULLONG_MAX;
	rc = cxl_cmd_submit(cmd);
	if (rc < 0)
		goto out;
	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0)
		goto out;

	alignment = cxl_cmd_identify_get_partition_align(cmd);
	if (alignment == 0) {
		log_err(&ml, "%s: no available capacity\n", devname);
		goto out;
	}

	/* Calculate the actual available capacity */
	total_cap = cxl_cmd_identify_get_total_size(cmd);
	volatile_only = cxl_cmd_identify_get_volatile_only_size(cmd);
	persistent_only = cxl_cmd_identify_get_persistent_only_size(cmd);
	available = total_cap - volatile_only - persistent_only;

	/* Translate the users size request into an aligned volatile_size */
	volatile_size = param_size_to_volatile_size(devname, type, size,
				available);
	if (volatile_size == ULLONG_MAX)
		goto out;

	volatile_size = partition_align(devname, type, volatile_size, alignment,
				available);

out:
	cxl_cmd_unref(cmd);
	return volatile_size;
}

static int action_setpartition(struct cxl_memdev *memdev,
			       struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	enum cxl_setpart_type type = CXL_SETPART_PMEM;
	unsigned long long size = ULLONG_MAX;
	struct json_object *jmemdev;
	unsigned long flags;
	struct cxl_cmd *cmd;
	int rc;

	if (param.type) {
		if (strcmp(param.type, "pmem") == 0)
			/* default */;
		else if (strcmp(param.type, "volatile") == 0)
			type = CXL_SETPART_VOLATILE;
		else if (strcmp(param.type, "ram") == 0)
			type = CXL_SETPART_VOLATILE;
		else {
			log_err(&ml, "invalid type '%s'\n", param.type);
			return -EINVAL;
		}
	}

	if (param.size) {
		size = parse_size64(param.size);
		if (size == ULLONG_MAX) {
			log_err(&ml, "%s: failed to parse size option '%s'\n",
			devname, param.size);
			return -EINVAL;
		}
	}

	size = validate_partition(memdev, type, size);
	if (size == ULLONG_MAX)
		return -EINVAL;

	cmd = cxl_cmd_new_set_partition(memdev, size);
	if (!cmd) {
		rc = -ENXIO;
		goto out_err;
	}

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		log_err(&ml, "cmd submission failed: %s\n", strerror(-rc));
		goto out_cmd;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		log_err(&ml, "%s: mbox status: %d\n", __func__, rc);
		rc = -ENXIO;
	}

out_cmd:
	cxl_cmd_unref(cmd);
out_err:
	if (rc)
		log_err(&ml, "%s error: %s\n", devname, strerror(-rc));

	flags = UTIL_JSON_PARTITION;
	if (actx->f_out == stdout && isatty(1))
		flags |= UTIL_JSON_HUMAN;
	jmemdev = util_cxl_memdev_to_json(memdev, flags);
	if (actx->jdevs && jmemdev)
		json_object_array_add(actx->jdevs, jmemdev);

	return rc;
}

static int memdev_action(int argc, const char **argv, struct cxl_ctx *ctx,
			 int (*action)(struct cxl_memdev *memdev,
				       struct action_context *actx),
			 const struct option *options, const char *usage)
{
	struct cxl_memdev *memdev, *single = NULL;
	struct action_context actx = { 0 };
	int i, rc = 0, count = 0, err = 0;
	const char * const u[] = {
		usage,
		NULL
	};
	unsigned long id;

	log_init(&ml, "cxl memdev", "CXL_MEMDEV_LOG");
	argc = parse_options(argc, argv, options, u, 0);

	if (argc == 0)
		usage_with_options(u, options);
	for (i = 0; i < argc; i++) {
		if (param.serial) {
			char *end;

			strtoull(argv[i], &end, 0);
			if (end[0] == 0)
				continue;
		} else {
			if (strcmp(argv[i], "all") == 0) {
				argc = 1;
				break;
			}
			if (sscanf(argv[i], "cxl%lu", &id) == 1)
				continue;
			if (sscanf(argv[i], "mem%lu", &id) == 1)
				continue;
			if (sscanf(argv[i], "%lu", &id) == 1)
				continue;
		}

		log_err(&ml, "'%s' is not a valid memdev %s\n", argv[i],
			param.serial ? "serial number" : "name");
		err++;
	}

	if (action == action_setpartition || action == action_reserve_dpa ||
	    action == action_free_dpa)
		actx.jdevs = json_object_new_array();

	if (err == argc) {
		usage_with_options(u, options);
		return -EINVAL;
	}

	if (!param.outfile)
		actx.f_out = stdout;
	else {
		actx.f_out = fopen(param.outfile, "w+");
		if (!actx.f_out) {
			log_err(&ml, "failed to open: %s: (%s)\n",
				param.outfile, strerror(errno));
			rc = -errno;
			goto out;
		}
	}

	if (!param.infile) {
		actx.f_in = stdin;
	} else {
		actx.f_in = fopen(param.infile, "r");
		if (!actx.f_in) {
			log_err(&ml, "failed to open: %s: (%s)\n", param.infile,
				strerror(errno));
			rc = -errno;
			goto out_close_fout;
		}
	}

	if (param.verbose) {
		cxl_set_log_priority(ctx, LOG_DEBUG);
		ml.log_priority = LOG_DEBUG;
	} else
		ml.log_priority = LOG_INFO;

	rc = 0;
	err = 0;
	count = 0;

	for (i = 0; i < argc; i++) {
		bool found = false;

		cxl_memdev_foreach(ctx, memdev) {
			const char *memdev_filter = NULL;
			const char *serial_filter = NULL;

			if (param.serial)
				serial_filter = argv[i];
			else
				memdev_filter = argv[i];

			if (!util_cxl_memdev_filter(memdev, memdev_filter,
						    serial_filter))
				continue;
			found = true;

			if (action == action_write ||
			    action == action_set_alert_config ||
			    action == action_transfer_firmware ||
			    action == action_activate_firmware) {
				single = memdev;
				rc = 0;
			} else
				rc = action(memdev, &actx);

			if (rc == 0)
				count++;
			else if (rc && !err)
				err = rc;
		}
		if (!found)
			log_info(&ml, "no memdev matches %s\n", argv[i]);
	}
	rc = err;

	if (action == action_write || action == action_set_alert_config ||
		action == action_transfer_firmware ||
		action == action_activate_firmware) {
		if (count > 1) {
			if (action == action_write)
				error("write-labels only supports writing a single memdev\n");
			else if (action == action_set_alert_config)
				error("set-alert-config only supports setting a single memdev\n");
			else if (action == action_transfer_firmware)
				error("transfer-firmware only supports transferring a single memdev\n");
			else if (action == action_activate_firmware)
				error("activate-firmware only supports activating a single memdev\n");
			usage_with_options(u, options);
			return -EINVAL;
		} else if (single) {
			rc = action(single, &actx);
			if (rc)
				count = 0;
		}
	}

	if (actx.f_in != stdin)
		fclose(actx.f_in);

	if (actx.jdevs) {
		unsigned long flags = 0;

		if (actx.f_out == stdout && isatty(1))
			flags |= UTIL_JSON_HUMAN;
		util_display_json_array(actx.f_out, actx.jdevs, flags);
	}


 out_close_fout:
	if (actx.f_out != stdout)
		fclose(actx.f_out);

 out:
	/*
	 * count if some actions succeeded, 0 if none were attempted,
	 * negative error code otherwise.
	 */
	if (count > 0)
		return count;
	return rc;
}

int cmd_write_labels(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(argc, argv, ctx, action_write, write_options,
			"cxl write-labels <memdev> [-i <filename>]");

	log_info(&ml, "wrote %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");
	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_read_labels(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(argc, argv, ctx, action_read, read_options,
			"cxl read-labels <mem0> [<mem1>..<memN>] [-o <filename>]");

	log_info(&ml, "read %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");
	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_zero_labels(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(argc, argv, ctx, action_zero, zero_options,
			"cxl zero-labels <mem0> [<mem1>..<memN>] [<options>]");

	log_info(&ml, "zeroed %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");
	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_disable_memdev(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_disable, disable_options,
		"cxl disable-memdev <mem0> [<mem1>..<memN>] [<options>]");

	log_info(&ml, "disabled %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");
	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_enable_memdev(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_enable, enable_options,
		"cxl enable-memdev <mem0> [<mem1>..<memN>] [<options>]");

	log_info(&ml, "enabled %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");
	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_set_partition(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(argc, argv, ctx, action_setpartition,
			set_partition_options,
			"cxl set-partition <mem0> [<mem1>..<memN>] [<options>]");
	log_info(&ml, "set_partition %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_reserve_dpa(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_reserve_dpa, reserve_dpa_options,
		"cxl reserve-dpa <mem0> [<mem1>..<memn>] [<options>]");
	log_info(&ml, "reservation completed on %d mem device%s\n",
		 count >= 0 ? count : 0, count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_free_dpa(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_free_dpa, free_dpa_options,
		"cxl free-dpa <mem0> [<mem1>..<memn>] [<options>]");
	log_info(&ml, "reservation release completed on %d mem device%s\n",
		 count >= 0 ? count : 0, count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

/************************************ smdk ***********************************/
int cmd_get_poison(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_get_poison, get_poison_options,
		"cxl get-poison <mem0> [<options>]");
	log_info(&ml, "get-poison %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_inject_poison(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_inject_poison, inject_poison_options,
		"cxl inject-poison <mem0> -a <dpa> [<options>]");
	log_info(&ml, "inject-poison %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_clear_poison(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_clear_poison, clear_poison_options,
		"cxl clear-poison <mem0> -a <dpa> [<options>]");
	log_info(&ml, "clear-poison %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_get_timestamp(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_get_timestamp, get_timestamp_options,
		"cxl get-timestamp <mem0> [<options>]");
	log_info(&ml, "get-timestamp %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_set_timestamp(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_set_timestamp, set_timestamp_options,
		"cxl set-timestamp <mem0> [<options>]");
	log_info(&ml, "set-timestamp %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_get_event_record(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_get_event_record, get_event_record_options,
		"cxl get-event-record <mem0> -t <event_type> [<options>]");
	log_info(&ml, "get-event-record %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_clear_event_record(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_clear_event_record, clear_event_record_options,
		"cxl clear-event-record <mem0> -t <event_type> [<options>]");
	log_info(&ml, "clear-event-record %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_identify(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_identify, identify_options,
		"cxl identify <mem0> [<mem1>..<memn>] [<options>]");
	log_info(&ml, "identified %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_get_health_info(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_get_health_info, get_health_info_options,
		"cxl get-health-info <mem0> [<mem1>..<memn>] [<options>]");
	log_info(&ml, "get-health-info %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_get_alert_config(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_get_alert_config,
		get_alert_config_options,
		"cxl get-alert-config <mem0> [<mem1>..<memn>] [<options>]");
	log_info(&ml, "get-alert-config %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_set_alert_config(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_set_alert_config,
		set_alert_config_options,
		"cxl set-alert-config <memdev> -e <event> -a <action> -t <threshold> [<options>]");
	log_info(&ml, "set-alert-config %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_get_firmware_info(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_get_firmware_info,
		get_firmware_info_options,
		"cxl get-firmware-info <mem0> [<mem1>..<memn>] [<options>]");
	log_info(&ml, "get-firmware-info %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_transfer_firmware(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_transfer_firmware,
		transfer_firmware_options,
		"cxl transfer-firmware <memdev> -i <firmware_package> -s <slot_number> [<options>]");
	log_info(&ml, "transfer-firmware %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_activate_firmware(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_activate_firmware,
		activate_firmware_options,
		"cxl activate-firmware <memdev> -s <slot_number> [<options>]");
	log_info(&ml, "activate-firmware %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}
