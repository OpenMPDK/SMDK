// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020-2021 Intel Corporation. All rights reserved. */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
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
	const char *bus;
	const char *outfile;
	const char *infile;
	const char *fw_file;
	unsigned len;
	unsigned offset;
	bool verbose;
	bool serial;
	bool force;
	bool align;
	bool cancel;
	bool wait;
	const char *type;
	const char *size;
	const char *decoder_filter;
	const char *life_used_threshold;
	const char *dev_over_temperature_threshold;
	const char *dev_under_temperature_threshold;
	const char *corrected_volatile_mem_err_threshold;
	const char *corrected_pmem_err_threshold;
	const char *life_used_alert;
	const char *dev_over_temperature_alert;
	const char *dev_under_temperature_alert;
	const char *corrected_volatile_mem_err_alert;
	const char *corrected_pmem_err_alert;
	int timeout;
} param = {
	.timeout = -1,
};

static struct log_ctx ml;

struct alert_context {
	int valid_alert_actions;
	int enable_alert_actions;
	int life_used_threshold;
	int dev_over_temperature_threshold;
	int dev_under_temperature_threshold;
	int corrected_volatile_mem_err_threshold;
	int corrected_pmem_err_threshold;
};

enum cxl_setalert_event {
	CXL_SETALERT_LIFE_USED,
	CXL_SETALERT_OVER_TEMP,
	CXL_SETALERT_UNDER_TEMP,
	CXL_SETALERT_VOLATILE_MEM_ERROR,
	CXL_SETALERT_PMEM_ERROR,
};

enum cxl_setpart_type {
	CXL_SETPART_PMEM,
	CXL_SETPART_VOLATILE,
};

#define BASE_OPTIONS() \
OPT_STRING('b', "bus", &param.bus, "bus name", \
	   "Limit operation to the specified bus"), \
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

#define FW_OPTIONS()                                                 \
OPT_STRING('F', "firmware-file", &param.fw_file, "firmware-file",     \
	   "firmware image file to use for the update"),             \
OPT_BOOLEAN('c', "cancel", &param.cancel,                            \
	    "attempt to abort an in-progress firmware update"),      \
OPT_BOOLEAN('w', "wait", &param.wait,                                \
	    "wait for firmware update to complete before returning")

#define SET_ALERT_OPTIONS()                                                   \
OPT_STRING('L', "life-used-threshold", &param.life_used_threshold,            \
	   "threshold", "threshold value for life used warning alert"),       \
OPT_STRING('\0', "life-used-alert", &param.life_used_alert,                   \
	   "'on' or 'off'", "enable or disable life used warning alert"),     \
OPT_STRING('O', "over-temperature-threshold",                                 \
	   &param.dev_over_temperature_threshold, "threshold",                \
	   "threshold value for device over temperature warning alert"),      \
OPT_STRING('\0', "over-temperature-alert",                                    \
	   &param.dev_over_temperature_alert, "'on' or 'off'",                \
	   "enable or disable device over temperature warning alert"),        \
OPT_STRING('U', "under-temperature-threshold",                                \
	   &param.dev_under_temperature_threshold, "threshold",               \
	   "threshold value for device under temperature warning alert"),     \
OPT_STRING('\0', "under-temperature-alert",                                   \
	   &param.dev_under_temperature_alert, "'on' or 'off'",               \
	   "enable or disable device under temperature warning alert"),       \
OPT_STRING('V', "volatile-mem-err-threshold",                                 \
	   &param.corrected_volatile_mem_err_threshold, "threshold",          \
	   "threshold value for corrected volatile mem error warning alert"), \
OPT_STRING('\0', "volatile-mem-err-alert",                                    \
	   &param.corrected_volatile_mem_err_alert, "'on' or 'off'",          \
	   "enable or disable corrected volatile mem error warning alert"),   \
OPT_STRING('P', "pmem-err-threshold",                                         \
	   &param.corrected_pmem_err_threshold, "threshold",                  \
	   "threshold value for corrected pmem error warning alert"),         \
OPT_STRING('\0', "pmem-err-alert",                                            \
	   &param.corrected_pmem_err_alert, "'on' or 'off'",                  \
	   "enable or disable corrected pmem error warning alert")

#define WAIT_SANITIZE_OPTIONS()                \
OPT_INTEGER('t', "timeout", &param.timeout,    \
	    "time in milliseconds to wait for overwrite completion (default: infinite)")

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

static const struct option update_fw_options[] = {
	BASE_OPTIONS(),
	FW_OPTIONS(),
	OPT_END(),
};

static const struct option set_alert_options[] = {
	BASE_OPTIONS(),
	SET_ALERT_OPTIONS(),
	OPT_END(),
};

static const struct option wait_sanitize_options[] = {
	BASE_OPTIONS(),
	WAIT_SANITIZE_OPTIONS(),
	OPT_END(),
};

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
	struct cxl_endpoint *ep;
	struct cxl_port *port;

	if (!cxl_memdev_is_enabled(memdev))
		return 0;

	ep = cxl_memdev_get_endpoint(memdev);
	if (!ep)
		return -ENODEV;

	port = cxl_endpoint_get_port(ep);
	if (!port)
		return -ENODEV;

	if (cxl_port_decoders_committed(port)) {
		log_err(&ml, "%s is part of an active region\n",
			cxl_memdev_get_devname(memdev));
		if (!param.force)
			return -EBUSY;

		log_err(&ml, "Forcing %s disable with an active region!\n",
			cxl_memdev_get_devname(memdev));
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

	if (param.len) {
		size = param.len;
	} else {
		size = cxl_memdev_get_label_size(memdev);
		if (size == SIZE_MAX)
			return -EINVAL;
	}

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

		if (label_size == SIZE_MAX)
			return -EINVAL;

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

	if (param.len) {
		size = param.len;
	} else {
		size = cxl_memdev_get_label_size(memdev);
		if (size == SIZE_MAX)
			return -EINVAL;
	}
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

static int action_wait_sanitize(struct cxl_memdev *memdev,
				struct action_context *actx)
{
	return cxl_memdev_wait_sanitize(memdev, param.timeout);
}

static int action_update_fw(struct cxl_memdev *memdev,
			    struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct json_object *jmemdev;
	unsigned long flags;
	int rc = 0;

	if (param.cancel)
		return cxl_memdev_cancel_fw_update(memdev);

	if (param.fw_file) {
		rc = cxl_memdev_update_fw(memdev, param.fw_file);
		if (rc)
			log_err(&ml, "%s error: %s\n", devname, strerror(-rc));
	}

	if (param.wait) {
		while (cxl_memdev_fw_update_in_progress(memdev) ||
		       cxl_memdev_fw_update_get_remaining(memdev) > 0)
			sleep(1);
	}

	flags = UTIL_JSON_FIRMWARE;
	if (actx->f_out == stdout && isatty(1))
		flags |= UTIL_JSON_HUMAN;
	jmemdev = util_cxl_memdev_to_json(memdev, flags);
	if (actx->jdevs && jmemdev)
		json_object_array_add(actx->jdevs, jmemdev);

	return rc;
}

static int validate_alert_threshold(enum cxl_setalert_event event,
				    int threshold)
{
	if (event == CXL_SETALERT_LIFE_USED) {
		if (threshold < 0 || threshold > 100) {
			log_err(&ml, "Invalid life used threshold: %d\n",
				threshold);
			return -EINVAL;
		}
	} else if (event == CXL_SETALERT_OVER_TEMP ||
		   event == CXL_SETALERT_UNDER_TEMP) {
		if (threshold < SHRT_MIN || threshold > SHRT_MAX) {
			log_err(&ml,
				"Invalid device temperature threshold: %d\n",
				threshold);
			return -EINVAL;
		}
	} else {
		if (threshold < 0 || threshold > USHRT_MAX) {
			log_err(&ml,
				"Invalid corrected mem error threshold: %d\n",
				threshold);
			return -EINVAL;
		}
	}
	return 0;
}

#define alert_param_set_threshold(arg, alert_event)                           \
{                                                                             \
	if (!param.arg##_alert) {                                             \
		if (param.arg##_threshold) {                                  \
			log_err(&ml, "Action not specified\n");               \
			return -EINVAL;                                       \
		}                                                             \
	} else if (strcmp(param.arg##_alert, "on") == 0) {                    \
		if (param.arg##_threshold) {                                  \
			char *endptr;                                         \
			alertctx.arg##_threshold =                            \
				strtol(param.arg##_threshold, &endptr, 10);   \
			if (endptr[0] != '\0') {                              \
				log_err(&ml, "Invalid threshold: %s\n",       \
					param.arg##_threshold);               \
				return -EINVAL;                               \
			}                                                     \
			rc = validate_alert_threshold(                        \
				alert_event, alertctx.arg##_threshold);       \
			if (rc != 0)                                          \
				return rc;                                    \
			alertctx.valid_alert_actions |= 1 << alert_event;     \
			alertctx.enable_alert_actions |= 1 << alert_event;    \
		} else {                                                      \
			log_err(&ml, "Threshold not specified\n");            \
			return -EINVAL;                                       \
		}                                                             \
	} else if (strcmp(param.arg##_alert, "off") == 0) {                   \
		if (!param.arg##_threshold) {                                 \
			alertctx.valid_alert_actions |= 1 << alert_event;     \
			alertctx.enable_alert_actions &= ~(1 << alert_event); \
		} else {                                                      \
			log_err(&ml, "Disable not require threshold\n");      \
			return -EINVAL;                                       \
		}                                                             \
	} else {                                                              \
		log_err(&ml, "Invalid action: %s\n", param.arg##_alert);      \
		return -EINVAL;                                               \
	}                                                                     \
}

#define setup_threshold_field(arg)                                            \
{                                                                             \
	if (param.arg##_threshold)                                            \
		cxl_cmd_alert_config_set_##arg##_prog_warn_threshold(         \
			cmd, alertctx.arg##_threshold);                       \
}

static int action_set_alert_config(struct cxl_memdev *memdev,
				   struct action_context *actx)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_cmd *cmd;
	struct alert_context alertctx = { 0 };
	struct json_object *jmemdev;
	unsigned long flags;
	int rc = 0;

	alert_param_set_threshold(life_used, CXL_SETALERT_LIFE_USED)
	alert_param_set_threshold(dev_over_temperature, CXL_SETALERT_OVER_TEMP)
	alert_param_set_threshold(dev_under_temperature,
				  CXL_SETALERT_UNDER_TEMP)
	alert_param_set_threshold(corrected_volatile_mem_err,
				  CXL_SETALERT_VOLATILE_MEM_ERROR)
	alert_param_set_threshold(corrected_pmem_err, CXL_SETALERT_PMEM_ERROR)
	if (alertctx.valid_alert_actions == 0) {
		log_err(&ml, "No action specified\n");
		return -EINVAL;
	}

	cmd = cxl_cmd_new_set_alert_config(memdev);
	if (!cmd) {
		rc = -ENXIO;
		goto out_err;
	}

	setup_threshold_field(life_used)
	setup_threshold_field(dev_over_temperature)
	setup_threshold_field(dev_under_temperature)
	setup_threshold_field(corrected_volatile_mem_err)
	setup_threshold_field(corrected_pmem_err)
	cxl_cmd_alert_config_set_valid_alert_actions(
		cmd, alertctx.valid_alert_actions);
	cxl_cmd_alert_config_set_enable_alert_actions(
		cmd, alertctx.enable_alert_actions);

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

	flags = UTIL_JSON_ALERT_CONFIG;
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
	    action == action_free_dpa || action == action_update_fw ||
	    action == action_set_alert_config)
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
			if (!util_cxl_memdev_filter_by_bus(memdev, param.bus))
				continue;
			found = true;

			if (action == action_write) {
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

	if (action == action_write) {
		if (count > 1) {
			error("write-labels only supports writing a single memdev\n");
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

int cmd_update_fw(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_update_fw, update_fw_options,
		"cxl update-firmware <mem0> [<mem1>..<memn>] [<options>]");
	const char *op_string;

	if (param.cancel)
		op_string = "cancelled";
	else if (param.wait)
		op_string = "completed";
	else
		op_string = "started";

	log_info(&ml, "firmware update %s on %d mem device%s\n", op_string,
		 count >= 0 ? count : 0, count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_set_alert_config(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_set_alert_config, set_alert_options,
		"cxl set-alert-config <mem0> [<mem1>..<memN>] [<options>]");
	log_info(&ml, "set alert configuration for %d mem%s\n",
		 count >= 0 ? count : 0, count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}

int cmd_wait_sanitize(int argc, const char **argv, struct cxl_ctx *ctx)
{
	int count = memdev_action(
		argc, argv, ctx, action_wait_sanitize, wait_sanitize_options,
		"cxl wait-sanitize <mem0> [<mem1>..<memn>] [<options>]");

	log_info(&ml, "wait sanitize %s on %d mem device%s\n",
		 count >= 0 ? "completed" : "failed", count >= 0 ? count : 0,
		 count > 1 ? "s" : "");

	return count >= 0 ? 0 : EXIT_FAILURE;
}
