// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020-2021 Intel Corporation. All rights reserved. */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <util/log.h>
#include <util/size.h>
#include <cxl/libcxl.h>
#include <util/parse-options.h>
#include <ccan/minmax/minmax.h>

#include "../filter.h"

struct action_context {
	FILE *f_out;
	FILE *f_in;
};

static struct parameters {
	const char *infile;
	bool verbose;
	bool serial;
	unsigned event_type;
	bool clear_all;
	unsigned int event_handle;
	const char *decoder_filter;
	const char *event_alert;
	const char *action;
	const char *threshold;
	const char *slot;
	bool online;
	bool shutdown_state_clean;
	bool no_event_log;
	bool sanitize;
	bool secure_erase;
	const char *poison_address;
	const char *poison_len;
} param;

static struct log_ctx ml;

enum cxl_setpart_type {
	CXL_SETPART_PMEM,
	CXL_SETPART_VOLATILE,
};

#define BASE_OPTIONS() \
OPT_BOOLEAN('v',"verbose", &param.verbose, "turn on debug"), \
OPT_BOOLEAN('S', "serial", &param.serial, "use serial numbers to id memdevs")

#define POISON_OPTIONS()                                       \
OPT_STRING('a', "address", &param.poison_address, "dpa",       \
	   "DPA to inject or clear poison"),                   \
OPT_STRING('l', "length", &param.poison_len, "dpa length",     \
	   "length in bytes from the DPA specified by '-a' to inject "\
	   "or clear poison")

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

#define SET_SHUTDOWN_STATE_OPTIONS() \
OPT_BOOLEAN('\0', "clean", &param.shutdown_state_clean, \
	"set shutdown state to clean. (default: set to dirty)")

#define GET_SCAN_MEDIA_CAPS_OPTIONS() \
OPT_STRING('a', "address", &param.poison_address, "dpa", \
	"The starting DPA where to retrieve Scan Media capabilities or to start the scan."), \
OPT_STRING('l', "length", &param.poison_len, "addr-length",\
	"range of physical addresses. Shall be in units of 64B. (hex value)")

#define SCAN_MEDIA_OPTIONS() \
OPT_BOOLEAN('\0', "no_event_log", &param.no_event_log, \
	"when set, the device shall not generate event logs for media errors " \
	"found during the Scan Media operation.")

#define SANITIZE_OPTIONS()			      \
OPT_BOOLEAN('e', "secure-erase", &param.secure_erase, \
	    "secure erase a memdev"),		      \
OPT_BOOLEAN('s', "sanitize", &param.sanitize,	      \
	    "sanitize a memdev")

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

static const struct option get_shutdown_state_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option set_shutdown_state_options[] = {
	BASE_OPTIONS(),
	SET_SHUTDOWN_STATE_OPTIONS(),
	OPT_END(),
};

static const struct option get_scan_media_caps_options[] = {
	BASE_OPTIONS(),
	GET_SCAN_MEDIA_CAPS_OPTIONS(),
	OPT_END(),
};

static const struct option scan_media_options[] = {
	BASE_OPTIONS(),
	GET_SCAN_MEDIA_CAPS_OPTIONS(),
	SCAN_MEDIA_OPTIONS(),
	OPT_END(),
};

static const struct option get_scan_media_options[] = {
	BASE_OPTIONS(),
	OPT_END(),
};

static const struct option sanitize_options[] = {
	BASE_OPTIONS(),
	SANITIZE_OPTIONS(),
	OPT_END(),
};

static const struct option poison_options[] = {
	BASE_OPTIONS(),
	POISON_OPTIONS(),
	OPT_END(),
};

static int validate_poison_dpa(struct cxl_memdev *memdev,
			       unsigned long long dpa,
			       unsigned long long length)
{
	unsigned long long avail_dpa;

	avail_dpa = cxl_memdev_get_pmem_size(memdev) +
		    cxl_memdev_get_ram_size(memdev);

	if (!avail_dpa) {
		log_err(&ml, "%s: no available dpa resource\n",
			cxl_memdev_get_devname(memdev));
		return -EINVAL;
	}
	if ((dpa + length) > avail_dpa) {
		log_err(&ml, "%s: dpa (range) exceeds the device resource\n",
			cxl_memdev_get_devname(memdev));
		return -EINVAL;
	}
	if (!IS_ALIGNED(dpa, 64)) {
		log_err(&ml, "%s: dpa:0x%llx is not 64-byte aligned\n",
			cxl_memdev_get_devname(memdev), dpa);
		return -EINVAL;
	}
	if (!length || !IS_ALIGNED(length, 64)) {
		log_err(&ml, "%s: invalid length:0x%llx\n",
			cxl_memdev_get_devname(memdev), length);
		return -EINVAL;
	}

	return 0;
}

static int action_inject_poison(struct cxl_memdev *memdev,
				struct action_context *actx)
{
	unsigned long long dpa, length, i;
	char buf[256];
	int rc;

	if (!param.poison_address) {
		log_err(&ml, "%s: set dpa to inject poison\n",
			cxl_memdev_get_devname(memdev));
		return -EINVAL;
	}
	dpa = parse_size64(param.poison_address);

	if (!param.poison_len)
		length = 64;
	else
		length = parse_size64(param.poison_len);

	rc = validate_poison_dpa(memdev, dpa, length);
	if (rc)
		goto out;

	for (i = 0; i < length; i += 64) {
		memset(buf, 0, sizeof(buf));
		rc = snprintf(buf, sizeof(buf), "0x%llx", dpa + i);
		if (rc >= (int)sizeof(buf)) {
			log_err(&ml, "%s: dpa '0x%llx' too long",
				cxl_memdev_get_devname(memdev), dpa + i);
			return -EINVAL;
		}
		rc = cxl_memdev_inject_poison(memdev, buf);
		if (rc < 0) {
			log_err(&ml, "%s: inject poison failed: %s\n",
				cxl_memdev_get_devname(memdev), strerror(-rc));
			goto out;
		}
	}

out:
	return rc;
}

static int action_clear_poison(struct cxl_memdev *memdev,
			       struct action_context *actx)
{
	unsigned long long dpa, length, i;
	char buf[256];
	int rc;

	if (!param.poison_address) {
		log_err(&ml, "%s: set dpa to clear poison.\n",
			cxl_memdev_get_devname(memdev));
		return -EINVAL;
	}
	dpa = parse_size64(param.poison_address);

	if (!param.poison_len)
		length = 64;
	else
		length = parse_size64(param.poison_len);

	rc = validate_poison_dpa(memdev, dpa, length);
	if (rc)
		goto out;

	for (i = 0; i < length; i += 64) {
		memset(buf, 0, sizeof(buf));
		rc = snprintf(buf, sizeof(buf), "0x%llx", dpa + i);
		if (rc >= (int)sizeof(buf)) {
			log_err(&ml, "%s: dpa '0x%llx' too long",
				cxl_memdev_get_devname(memdev), dpa + i);
			return -EINVAL;
		}
		rc = cxl_memdev_clear_poison(memdev, buf);
		if (rc < 0) {
			log_err(&ml, "%s: clear poison failed: %s\n",
				cxl_memdev_get_devname(memdev), strerror(-rc));
			goto out;
		}
	}

out:
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
	struct cxl_cmd *cmd;
	int rc;

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
	printf("CXL Get Health Information Memory Device \"%s\"\n",
	       cxl_memdev_get_devname(memdev));
	cxl_memdev_print_get_health_info(memdev, cmd);

out:
	cxl_cmd_unref(cmd);
	return rc;
}

static int action_get_alert_config(struct cxl_memdev *memdev,
				   struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	int v_critical, v_warning, rc;

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

	printf("Life Used Threshold - Critical                        : ");
	v_critical =
		cxl_cmd_alert_config_get_life_used_crit_alert_threshold(cmd);
	printf("%d %%\n", v_critical);
	printf("                    - Warning                         : ");
	if (!cxl_cmd_alert_config_life_used_prog_warn_threshold_valid(cmd))
		printf("Not Supported\n");
	else {
		v_warning =
			cxl_cmd_alert_config_get_life_used_prog_warn_threshold(
				cmd);
		printf("%d %% %s\n", v_warning,
		       cxl_cmd_alert_config_life_used_prog_warn_threshold_writable(
			       cmd) ?
			       "" :
			       "(Not programmable)");
	}

	printf("Device Over-Temperature Threshold - Critical          : ");
	v_critical =
		cxl_cmd_alert_config_get_dev_over_temperature_crit_alert_threshold(
			cmd);
	printf("%hd C\n", v_critical);
	printf("                                  - Warning           : ");
	if (!cxl_cmd_alert_config_dev_over_temperature_prog_warn_threshold_valid(
		    cmd))
		printf("Not Supported\n");
	else {
		v_warning =
			cxl_cmd_alert_config_get_dev_over_temperature_prog_warn_threshold(
				cmd);
		printf("%hd C %s\n", v_warning,
		       cxl_cmd_alert_config_dev_over_temperature_prog_warn_threshold_writable(
			       cmd) ?
			       "" :
			       "(Not programmable)");
	}

	printf("Device Under-Temperature Threshold - Critical         : ");
	v_critical =
		cxl_cmd_alert_config_get_dev_under_temperature_crit_alert_threshold(
			cmd);
	printf("%hd C\n", v_critical);
	printf("                                   - Warning          : ");
	if (!cxl_cmd_alert_config_dev_under_temperature_prog_warn_threshold_valid(
		    cmd))
		printf("Not Supported\n");
	else {
		v_warning =
			cxl_cmd_alert_config_get_dev_under_temperature_prog_warn_threshold(
				cmd);
		printf("%hd C %s\n", v_warning,
		       cxl_cmd_alert_config_dev_under_temperature_prog_warn_threshold_writable(
			       cmd) ?
			       "" :
			       "(Not programmable)");
	}

	printf("Corrected Volatile Memory Error Threshold - Warning   : ");
	if (!cxl_cmd_alert_config_corrected_volatile_mem_err_prog_warn_threshold_valid(
		    cmd))
		printf("Not Supported\n");
	else {
		v_warning =
			cxl_cmd_alert_config_get_corrected_volatile_mem_err_prog_warn_threshold(
				cmd);
		printf("%d %s\n", v_warning,
		       cxl_cmd_alert_config_corrected_volatile_mem_err_prog_warn_threshold_writable(
			       cmd) ?
			       "" :
			       "(Not programmable)");
	}

	printf("Corrected Persistent Memory Error Threshold - Warning : ");
	if (!cxl_cmd_alert_config_corrected_pmem_err_prog_warn_threshold_valid(
		    cmd))
		printf("Not Supported\n");
	else {
		v_warning =
			cxl_cmd_alert_config_get_corrected_pmem_err_prog_warn_threshold(
				cmd);
		printf("%d %s\n", v_warning,
		       cxl_cmd_alert_config_corrected_pmem_err_prog_warn_threshold_writable(
			       cmd) ?
			       "" :
			       "(Not programmable)");
	}

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
	for (int i = 1; i <= slots_supported; i++) {
		printf("Slot %d FW revision           : %s", i, fw_revs[i - 1]);
		if (i == active_slot)
			printf(" (Active)\n");
		else if (i == staged_slot)
			printf(" (Staged)\n");
		else
			printf("\n");
	}
	printf("Online Activation Capability : %s\n",
	       online_activation ? "Supported" : "Not Supported");

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

static int action_get_shutdown_state(struct cxl_memdev *memdev,
				     struct action_context *actx)
{
	int rc;
	rc = cxl_memdev_get_shutdown_state(memdev);
	if (rc < 0) {
		log_err(&ml, "%s: get shutdown state failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_set_shutdown_state(struct cxl_memdev *memdev,
				     struct action_context *actx)
{
	int rc;
	bool is_clean;

	is_clean = param.shutdown_state_clean;

	rc = cxl_memdev_set_shutdown_state(memdev, is_clean);
	if (rc < 0) {
		log_err(&ml, "%s: set shutdown state failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_get_scan_media_caps(struct cxl_memdev *memdev,
				      struct action_context *actx)
{
	int rc;
	unsigned long addr, length;
	if (!param.poison_address) {
		log_err(&ml, "%s: get scan media caps failed: %s\n",
			cxl_memdev_get_devname(memdev),
			"address should be provided");
		return -EINVAL;
	}
	addr = (unsigned long)strtol(param.poison_address, NULL, 16);

	if (!param.poison_len) {
		log_err(&ml, "%s: get scan media caps failed: %s\n",
			cxl_memdev_get_devname(memdev),
			"length should be provided");
		return -EINVAL;
	}
	length = (unsigned long)strtol(param.poison_len, NULL, 16);

	rc = validate_poison_dpa(memdev, addr, length * (unsigned long long)64);
	if (rc) {
		log_err(&ml, "%s: get scan media caps failed: %s\n",
			cxl_memdev_get_devname(memdev),
			"invalid address and(or) length");
		return -EINVAL;
	}

	rc = cxl_memdev_get_scan_media_caps(memdev, addr, length);
	if (rc < 0) {
		log_err(&ml, "%s: get scan media caps failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_scan_media(struct cxl_memdev *memdev,
			     struct action_context *actx)
{
	int rc;
	unsigned long addr, length;
	unsigned char flag = 0;

	if (!param.poison_address) {
		log_err(&ml, "%s: scan media failed: %s\n",
			cxl_memdev_get_devname(memdev),
			"address should be provided");
		return -EINVAL;
	}
	addr = (unsigned long)strtol(param.poison_address, NULL, 16);

	if (!param.poison_len) {
		log_err(&ml, "%s: scan media failed: %s\n",
			cxl_memdev_get_devname(memdev),
			"length should be provided");
		return -EINVAL;
	}
	length = (unsigned long)strtol(param.poison_len, NULL, 16);

	rc = validate_poison_dpa(memdev, addr, length * (unsigned long long)64);
	if (rc) {
		log_err(&ml, "%s: scan media failed: %s\n",
			cxl_memdev_get_devname(memdev),
			"invalid address and(or) length");
		return -EINVAL;
	}

	if (param.no_event_log)
		flag |= (1 << 0);

	rc = cxl_memdev_scan_media(memdev, addr, length, flag);
	if (rc < 0) {
		log_err(&ml, "%s: scan media failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_get_scan_media(struct cxl_memdev *memdev,
				 struct action_context *actx)
{
	int rc;
	rc = cxl_memdev_get_scan_media(memdev);
	if (rc < 0) {
		log_err(&ml, "%s: get scan media failed: %s\n",
			cxl_memdev_get_devname(memdev), strerror(-rc));
	}
	return rc;
}

static int action_sanitize_memdev(struct cxl_memdev *memdev,
				  struct action_context *actx)
{
	int rc = 0;

	/* let Sanitize be the default */
	if (!param.secure_erase && !param.sanitize)
		param.sanitize = true;

	if (param.secure_erase)
		/* TODO: Not support for now */
		rc = -EINVAL;
	else if (param.sanitize)
		rc = cxl_memdev_sanitize(memdev, "sanitize");
	else
		rc = -EINVAL;

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

	if (err == argc) {
		usage_with_options(u, options);
		return -EINVAL;
	}

	if (!param.infile) {
		actx.f_in = stdin;
	} else {
		actx.f_in = fopen(param.infile, "r");
		if (!actx.f_in) {
			log_err(&ml, "failed to open: %s: (%s)\n", param.infile,
				strerror(errno));
			rc = -errno;
			goto out;
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

			if (action == action_set_alert_config ||
			    action == action_transfer_firmware ||
			    action == action_activate_firmware ||
			    action == action_get_shutdown_state ||
			    action == action_set_shutdown_state ||
			    action == action_inject_poison ||
			    action == action_clear_poison) {
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

	if (action == action_set_alert_config ||
	    action == action_transfer_firmware ||
	    action == action_activate_firmware ||
	    action == action_get_shutdown_state ||
	    action == action_set_shutdown_state ||
	    action == action_inject_poison || action == action_clear_poison) {
		if (count > 1) {
			if (action == action_set_alert_config)
				error("set-alert-config only supports setting a single memdev\n");
			else if (action == action_transfer_firmware)
				error("transfer-firmware only supports transferring a single memdev\n");
			else if (action == action_activate_firmware)
				error("activate-firmware only supports activating a single memdev\n");
			else /* shutdown state */
				error("{get|set}-shutdown-state only supports a single memdev\n");
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

 out:
	/*
	 * count if some actions succeeded, 0 if none were attempted,
	 * negative error code otherwise.
	 */
	if (count > 0)
		return count;
	return rc;
}

int cmd_inject_poison(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_inject_poison, poison_options,
		"cxl inject-poison <memdev> -a <dpa> [<options>]");
	log_info(&ml, "inject-poison %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_clear_poison(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_clear_poison, poison_options,
		"cxl clear-poison <memdev> -a <dpa> [<options>]");
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

int cmd_get_shutdown_state(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(argc, argv, ctx, action_get_shutdown_state,
				  get_shutdown_state_options,
				  "cxl get-shutdown-state <mem0> [<options>]");
	log_info(&ml, "get-shutdown-state %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_set_shutdown_state(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(argc, argv, ctx, action_set_shutdown_state,
				  set_shutdown_state_options,
				  "cxl set-shutdown-state <mem0> [<options>]");
	log_info(&ml, "set-shutdown-state %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_get_scan_media_caps(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_get_scan_media_caps,
		get_scan_media_caps_options,
		"cxl get-scan-media-caps <mem0> [<mem1>..<memn>] -a <dpa> -l <length> [<options>]");
	log_info(&ml, "get-scan-media-caps %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_scan_media(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_scan_media, scan_media_options,
		"cxl scan-media <mem0> [<mem1>..<memn>] -a <dpa> -l <length> [<options>]");
	log_info(&ml, "scan-media %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_get_scan_media(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_get_scan_media, get_scan_media_options,
		"cxl get-scan-media <mem0> [<mem1>..<memn>] [<options>]");
	log_info(&ml, "get-scan-media %d mem%s\n", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_sanitize_memdev(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_sanitize_memdev, sanitize_options,
		"cxl sanitize-memdev <mem0> [<mem1>..<memn>] [<options>]");
	log_info(&ml, "sanitation started on %d mem device%s\n",
		 count >= 0 ? count : 0, count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}
