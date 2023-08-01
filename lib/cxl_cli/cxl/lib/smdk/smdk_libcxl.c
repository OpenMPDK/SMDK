// SPDX-License-Identifier: LGPL-2.1
// Copyright (C) 2020-2021, Intel Corporation. All rights reserved.
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <uuid/uuid.h>
#include <ccan/list/list.h>
#include <ccan/endian/endian.h>
#include <ccan/minmax/minmax.h>
#include <ccan/array_size/array_size.h>
#include <ccan/short_types/short_types.h>
#include <ccan/container_of/container_of.h>

#include <util/log.h>
#include <util/list.h>
#include <util/size.h>
#include <util/sysfs.h>
#include <util/bitmap.h>
#include <cxl/cxl_mem.h>
#include <cxl/libcxl.h>
#include "smdk_private.h"

/*************************Copied from libcxl.c********************************/
struct cxl_ctx {
	/* log_ctx must be first member for cxl_set_log_fn compat */
	struct log_ctx ctx;
	int refcount;
	void *userdata;
	int memdevs_init;
	int buses_init;
	unsigned long timeout;
	struct udev *udev;
	struct udev_queue *udev_queue;
	struct list_head memdevs;
	struct list_head buses;
	struct kmod_ctx *kmod_ctx;
	void *private_data;
};

static int cxl_cmd_alloc_query(struct cxl_cmd *cmd, int num_cmds)
{
	size_t size;

	if (!cmd)
		return -EINVAL;

	if (cmd->query_cmd != NULL)
		free(cmd->query_cmd);

	size = struct_size(cmd->query_cmd, commands, num_cmds);
	if (size == SIZE_MAX)
		return -EOVERFLOW;

	cmd->query_cmd = calloc(1, size);
	if (!cmd->query_cmd)
		return -ENOMEM;

	cmd->query_cmd->n_commands = num_cmds;

	return 0;
}

static struct cxl_cmd *cxl_cmd_new(struct cxl_memdev *memdev)
{
	struct cxl_cmd *cmd;
	size_t size;

	size = sizeof(*cmd);
	cmd = calloc(1, size);
	if (!cmd)
		return NULL;

	cxl_cmd_ref(cmd);
	cmd->memdev = memdev;

	return cmd;
}

static int __do_cmd(struct cxl_cmd *cmd, int ioctl_cmd, int fd)
{
	void *cmd_buf;
	int rc;

	switch (ioctl_cmd) {
	case CXL_MEM_QUERY_COMMANDS:
		cmd_buf = cmd->query_cmd;
		break;
	case CXL_MEM_SEND_COMMAND:
		cmd_buf = cmd->send_cmd;
		break;
	default:
		return -EINVAL;
	}

	rc = ioctl(fd, ioctl_cmd, cmd_buf);
	if (rc < 0)
		rc = -errno;

	return rc;
}

static int do_cmd(struct cxl_cmd *cmd, int ioctl_cmd)
{
	char *path;
	struct stat st;
	unsigned int major, minor;
	int rc = 0, fd;
	struct cxl_memdev *memdev = cmd->memdev;
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	const char *devname = cxl_memdev_get_devname(memdev);

	major = cxl_memdev_get_major(memdev);
	minor = cxl_memdev_get_minor(memdev);

	if (asprintf(&path, "/dev/cxl/%s", devname) < 0)
		return -ENOMEM;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		err(ctx, "failed to open %s: %s\n", path, strerror(errno));
		rc = -errno;
		goto out;
	}

	if (fstat(fd, &st) >= 0 && S_ISCHR(st.st_mode) &&
	    major(st.st_rdev) == major && minor(st.st_rdev) == minor) {
		rc = __do_cmd(cmd, ioctl_cmd, fd);
	} else {
		err(ctx, "failed to validate %s as a CXL memdev node\n", path);
		rc = -ENXIO;
	}
	close(fd);
out:
	free(path);
	return rc;
}

static int alloc_do_query(struct cxl_cmd *cmd, int num_cmds)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(cmd->memdev);
	int rc;

	rc = cxl_cmd_alloc_query(cmd, num_cmds);
	if (rc)
		return rc;

	rc = do_cmd(cmd, CXL_MEM_QUERY_COMMANDS);
	if (rc < 0)
		err(ctx, "%s: query commands failed: %s\n",
		    cxl_memdev_get_devname(cmd->memdev), strerror(-rc));
	return rc;
}

static int cxl_cmd_do_query(struct cxl_cmd *cmd)
{
	struct cxl_memdev *memdev = cmd->memdev;
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	const char *devname = cxl_memdev_get_devname(memdev);
	int rc, n_commands;

	switch (cmd->query_status) {
	case CXL_CMD_QUERY_OK:
		return 0;
	case CXL_CMD_QUERY_UNSUPPORTED:
		return -EOPNOTSUPP;
	case CXL_CMD_QUERY_NOT_RUN:
		break;
	default:
		err(ctx, "%s: Unknown query_status %d\n", devname,
		    cmd->query_status);
		return -EINVAL;
	}

	rc = alloc_do_query(cmd, 0);
	if (rc)
		return rc;

	n_commands = cmd->query_cmd->n_commands;
	dbg(ctx, "%s: supports %d commands\n", devname, n_commands);

	return alloc_do_query(cmd, n_commands);
}

static int cxl_cmd_validate(struct cxl_cmd *cmd, u32 cmd_id)
{
	struct cxl_memdev *memdev = cmd->memdev;
	struct cxl_mem_query_commands *query = cmd->query_cmd;
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	u32 i;

	for (i = 0; i < query->n_commands; i++) {
		struct cxl_command_info *cinfo = &query->commands[i];
		const char *cmd_name = cxl_command_names[cinfo->id].name;

		if (cinfo->id != cmd_id)
			continue;

		dbg(ctx, "%s: %s: in: %d, out %d, flags: %#08x\n", devname,
		    cmd_name, cinfo->size_in, cinfo->size_out, cinfo->flags);

		cmd->query_idx = i;
		cmd->query_status = CXL_CMD_QUERY_OK;
		return 0;
	}
	cmd->query_status = CXL_CMD_QUERY_UNSUPPORTED;
	return -EOPNOTSUPP;
}

static int cxl_cmd_alloc_send(struct cxl_cmd *cmd, u32 cmd_id)
{
	struct cxl_mem_query_commands *query = cmd->query_cmd;
	struct cxl_command_info *cinfo = &query->commands[cmd->query_idx];
	size_t size;

	size = sizeof(struct cxl_send_command);
	cmd->send_cmd = calloc(1, size);
	if (!cmd->send_cmd)
		return -ENOMEM;

	if (cinfo->id != cmd_id)
		return -EINVAL;

	cmd->send_cmd->id = cmd_id;

	if (cinfo->size_in > 0) {
		cmd->input_payload = calloc(1, cinfo->size_in);
		if (!cmd->input_payload)
			return -ENOMEM;
		cmd->send_cmd->in.payload = (u64)cmd->input_payload;
		cmd->send_cmd->in.size = cinfo->size_in;
	}
	if (cinfo->size_out > 0) {
		cmd->output_payload = calloc(1, cinfo->size_out);
		if (!cmd->output_payload)
			return -ENOMEM;
		cmd->send_cmd->out.payload = (u64)cmd->output_payload;
		cmd->send_cmd->out.size = cinfo->size_out;
	}

	return 0;
}

static struct cxl_cmd *cxl_cmd_new_generic(struct cxl_memdev *memdev,
					   u32 cmd_id)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc;

	cmd = cxl_cmd_new(memdev);
	if (!cmd)
		return NULL;

	rc = cxl_cmd_do_query(cmd);
	if (rc) {
		err(ctx, "%s: query returned: %s\n", devname, strerror(-rc));
		goto fail;
	}

	rc = cxl_cmd_validate(cmd, cmd_id);
	if (rc) {
		errno = -rc;
		goto fail;
	}

	rc = cxl_cmd_alloc_send(cmd, cmd_id);
	if (rc) {
		errno = -rc;
		goto fail;
	}

	cmd->status = 1;
	return cmd;

fail:
	cxl_cmd_unref(cmd);
	return NULL;
}

static int cxl_cmd_validate_status(struct cxl_cmd *cmd, u32 id)
{
	if (cmd->send_cmd->id != id)
		return -EINVAL;
	if (cmd->status < 0)
		return cmd->status;
	return 0;
}

/* Helpers for health_info fields (no endian conversion) */
#define cmd_get_field_u8(cmd, n, N, field)                                     \
do {                                                                   \
	struct cxl_cmd_##n *c =                                        \
		(struct cxl_cmd_##n *)cmd->send_cmd->out.payload;      \
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_##N); \
	if (rc)                                                        \
		return rc;                                             \
	return c->field;                                               \
} while (0)

#define cmd_get_field_u16(cmd, n, N, field)                                    \
do {                                                                   \
	struct cxl_cmd_##n *c =                                        \
		(struct cxl_cmd_##n *)cmd->send_cmd->out.payload;      \
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_##N); \
	if (rc)                                                        \
		return rc;                                             \
	return le16_to_cpu(c->field);                                  \
} while (0)

#define cmd_get_field_u32(cmd, n, N, field)                                    \
do {                                                                   \
	struct cxl_cmd_##n *c =                                        \
		(struct cxl_cmd_##n *)cmd->send_cmd->out.payload;      \
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_##N); \
	if (rc)                                                        \
		return rc;                                             \
	return le32_to_cpu(c->field);                                  \
} while (0)

#define cmd_get_field_u8_mask(cmd, n, N, field, mask)                          \
do {                                                                   \
	struct cxl_cmd_##n *c =                                        \
		(struct cxl_cmd_##n *)cmd->send_cmd->out.payload;      \
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_##N); \
	if (rc)                                                        \
		return rc;                                             \
	return !!(c->field & mask);                                    \
} while (0)
/********************************************************************/

static void print_timestamp(unsigned long timestamp)
{
	time_t t;
	struct tm tm;

	timestamp = timestamp / nano_scale;
	t = (time_t)timestamp;
	tm = *localtime(&t);
	printf("%d/%d/%d %02d:%02d:%02d\n", tm.tm_mon + 1, tm.tm_mday,
	       tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

CXL_EXPORT int cxl_memdev_get_payload_max(struct cxl_memdev *memdev)
{
	return memdev->payload_max;
}

CXL_EXPORT struct cxl_cmd *
cxl_cmd_new_get_scan_media_caps(struct cxl_memdev *memdev,
				unsigned long address, unsigned long length)
{
	struct cxl_cmd_get_scan_media_caps_in *get_scan_media_caps;
	struct cxl_cmd *cmd;

	cmd = cxl_cmd_new_generic(memdev,
				  CXL_MEM_COMMAND_ID_GET_SCAN_MEDIA_CAPS);
	if (!cmd)
		return NULL;

	get_scan_media_caps = (struct cxl_cmd_get_scan_media_caps_in *)
				      cmd->send_cmd->in.payload;
	get_scan_media_caps->dpa = cpu_to_le64(address);
	get_scan_media_caps->len = cpu_to_le64(length);
	return cmd;
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_scan_media(struct cxl_memdev *memdev,
						  u8 flag,
						  unsigned long address,
						  unsigned long length)
{
	struct cxl_cmd_scan_media_in *scan_media;
	struct cxl_cmd *cmd;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_SCAN_MEDIA);
	if (!cmd)
		return NULL;

	scan_media = (struct cxl_cmd_scan_media_in *)cmd->send_cmd->in.payload;
	scan_media->dpa = cpu_to_le64(address);
	scan_media->len = cpu_to_le64(length);
	scan_media->flag = flag;

	return cmd;
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_get_scan_media(struct cxl_memdev *memdev)
{
	struct cxl_cmd *cmd;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_GET_SCAN_MEDIA);
	if (!cmd)
		return NULL;

	return cmd;
}

char *poison_source_type(unsigned long source_flag)
{
	char *source = "";
	switch (source_flag) {
	case 1:
		source = "External";
		break;
	case 2:
		source = "Internal";
		break;
	case 3:
		source = "Injected";
		break;
	case 7:
		source = "Vendor Specific";
		break;
	default:
		source = "Unknown";
	}

	return source;
}

static void print_poison_list(struct media_error_record *rcd, int count)
{
	int i;
	unsigned long address;
	unsigned long source;

	for (i = 0; i < count; i++) {
		source = rcd[i].dpa & POISON_SOURCE_MASK;
		address = rcd[i].dpa & POISON_ADDR_MASK;
		printf("%d. Physical Address : 0x%-18lx, Source : %s\n", i + 1,
		       address, poison_source_type(source));
	}
}

static int cxl_cmd_scan_media_caps_get_estimated_scan_time(struct cxl_cmd *cmd)
{
	unsigned int *time;
	int rc = 0;

	rc = cxl_cmd_validate_status(cmd,
				     CXL_MEM_COMMAND_ID_GET_SCAN_MEDIA_CAPS);
	if (rc)
		return rc;
	time = (unsigned int *)cmd->output_payload;
	if (!time)
		return -ENXIO;

	if (*time)
		printf("Estimated Scan Media Time(ms): %u\n", *time);
	else
		printf("Cannot estimate a Scan Media Time for the specified range.\n");
	return rc;
}

static int cxl_cmd_scan_media_get_results(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_scan_media *ret;
	int rc;

	rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_SCAN_MEDIA);
	if (rc)
		return rc;

	ret = (struct cxl_cmd_get_scan_media *)cmd->output_payload;
	if (!ret)
		return -ENXIO;

	if (ret->count == 0) {
		printf("No poison address\n");
	} else {
		print_poison_list(ret->rcd, (int)ret->count);
	}

	if (ret->flag & CXL_CMD_GET_SCAN_MEDIA_FLAGS_SCAN_STOPPED_PREMATURELY) {
		return RESULT_GET_SCAN_MEDIA_STOPPED_PREMATURELY;
	}
	if (ret->flag & CXL_CMD_GET_SCAN_MEDIA_FLAGS_MORE_MEDIA_ERROR_RECORDS) {
		return RESULT_GET_SCAN_MEDIA_MORE_RECORDS;
	}

	return rc;
}

#define LENGTH_UNIT (64)
static int poison_op(struct cxl_memdev *memdev, int op, u8 flag,
		     unsigned long address, unsigned long length)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	struct cxl_cmd_get_scan_media *out_payload;
	int rc = 0;
	unsigned long address_total;

	if (op != POISON_GET_SCAN_MEDIA) {
		if (op == POISON_SCAN_MEDIA)
			address_total = address + (length * LENGTH_UNIT);
		else
			address_total = address;

		if (memdev->pmem_size + memdev->ram_size < address_total)
			return -EINVAL;

		address -= address % 64;
	}

	switch (op) {
	case POISON_GET_SCAN_MEDIA_CAPS:
		cmd = cxl_cmd_new_get_scan_media_caps(memdev, address, length);
		if (!cmd)
			return -ENOMEM;
		break;
	case POISON_SCAN_MEDIA:
		if (length == 0)
			length = (memdev->pmem_size + memdev->ram_size) /
				 LENGTH_UNIT;
		cmd = cxl_cmd_new_scan_media(memdev, flag, address, length);
		if (!cmd)
			return -ENOMEM;
		break;
	case POISON_GET_SCAN_MEDIA:
		cmd = cxl_cmd_new_get_scan_media(memdev);
		if (!cmd)
			return -ENOMEM;
		rc = cxl_cmd_set_output_payload(cmd, NULL, memdev->payload_max);
		if (rc) {
			err(ctx, "%s: cmd setup failed: %s\n",
			    cxl_memdev_get_devname(memdev), strerror(-rc));
			goto out;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n", devname,
		    strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n", devname, rc);
		rc = -ENXIO;
		goto out;
	}

	switch (op) {
	case POISON_GET_SCAN_MEDIA_CAPS:
		rc = cxl_cmd_scan_media_caps_get_estimated_scan_time(cmd);
		break;
	case POISON_GET_SCAN_MEDIA:
		rc = cxl_cmd_scan_media_get_results(cmd);
		if (rc == RESULT_GET_SCAN_MEDIA_MORE_RECORDS) {
			cxl_cmd_unref(cmd);
			return cxl_memdev_get_scan_media(memdev);
		} else if (rc == RESULT_GET_SCAN_MEDIA_STOPPED_PREMATURELY) {
			out_payload = (struct cxl_cmd_get_scan_media *)
					      cmd->output_payload;
			address = (unsigned long)out_payload->dpa_restart;
			length = (unsigned long)out_payload->len_restart;
			cxl_cmd_unref(cmd);
			rc = cxl_memdev_scan_media(memdev, address, length, 0);
			/* Assume the above command operates as foreground */
			if (!rc)
				return cxl_memdev_get_scan_media(memdev);
			else
				return rc;
		}
		break;
	default:
		break;
	}

out:
	cxl_cmd_unref(cmd);
	return rc;
}

#define PATH_DEBUG_FS "/sys/kernel/debug/cxl"
CXL_EXPORT int cxl_memdev_inject_poison(struct cxl_memdev *memdev,
					const char *address)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	char *path = memdev->dev_buf;
	int len = memdev->buf_len, rc;

	if (snprintf(path, len, "%s/%s/inject_poison", PATH_DEBUG_FS,
		     cxl_memdev_get_devname(memdev)) >= len) {
		err(ctx, "%s: buffer too small\n",
		    cxl_memdev_get_devname(memdev));
		return -ENXIO;
	}
	rc = sysfs_write_attr(ctx, path, address);
	if (rc < 0) {
		err(ctx, "%s: failed to inject poison at %s\n",
		    cxl_memdev_get_devname(memdev), address);
		return rc;
	}

	dbg(ctx, "%s: poison injected at %s\n", cxl_memdev_get_devname(memdev),
	    address);

	return 0;
}

CXL_EXPORT int cxl_memdev_clear_poison(struct cxl_memdev *memdev,
				       const char *address)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	char *path = memdev->dev_buf;
	int len = memdev->buf_len, rc;

	if (snprintf(path, len, "%s/%s/clear_poison", PATH_DEBUG_FS,
		     cxl_memdev_get_devname(memdev)) >= len) {
		err(ctx, "%s: buffer too small\n",
		    cxl_memdev_get_devname(memdev));
		return -ENXIO;
	}
	rc = sysfs_write_attr(ctx, path, address);
	if (rc < 0) {
		err(ctx, "%s: failed to clear poison at %s\n",
		    cxl_memdev_get_devname(memdev), address);
		return rc;
	}

	dbg(ctx, "%s: poison cleared at %s\n", cxl_memdev_get_devname(memdev),
	    address);

	return 0;
}

CXL_EXPORT int cxl_memdev_get_scan_media_caps(struct cxl_memdev *memdev,
					      unsigned long address,
					      unsigned long length)
{
	return poison_op(memdev, POISON_GET_SCAN_MEDIA_CAPS, 0, address,
			 length);
}

CXL_EXPORT int cxl_memdev_scan_media(struct cxl_memdev *memdev,
				     unsigned long address,
				     unsigned long length, u8 flag)
{
	return poison_op(memdev, POISON_SCAN_MEDIA, flag, address, length);
}

CXL_EXPORT int cxl_memdev_get_scan_media(struct cxl_memdev *memdev)
{
	return poison_op(memdev, POISON_GET_SCAN_MEDIA, 0, 0, 0);
}

CXL_EXPORT int cxl_memdev_set_timestamp(struct cxl_memdev *memdev,
					unsigned long curr_time)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc = 0;

	unsigned long *set_time;
	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_SET_TIME);
	if (!cmd)
		return -ENOMEM;
	set_time = (unsigned long *)cmd->send_cmd->in.payload;
	*set_time = cpu_to_le64(curr_time);
	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n", devname,
		    strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n", devname, rc);
		rc = -ENXIO;
	}
out:
	cxl_cmd_unref(cmd);
	return rc;
}

CXL_EXPORT int cxl_cmd_get_timestamp_get_payload(struct cxl_cmd *cmd)
{
	unsigned long *ret;
	int rc = 0;

	rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_TIME);
	if (rc)
		return rc;
	ret = (unsigned long *)cmd->send_cmd->out.payload;
	if (!ret)
		return -ENXIO;
	print_timestamp(*ret);
	return 1;
}

CXL_EXPORT int cxl_cmd_get_shutdown_state_payload(struct cxl_cmd *cmd)
{
	u8 *state;
	int rc = 0;

	rc = cxl_cmd_validate_status(cmd,
				     CXL_MEM_COMMAND_ID_GET_SHUTDOWN_STATE);
	if (rc)
		return rc;
	state = (u8 *)cmd->output_payload;
	if (!state)
		return -ENXIO;
	printf("Shutdown State: %s\n", ((*state) & BIT(0)) ? "Dirty" : "Clean");
	return rc;
}

CXL_EXPORT int cxl_memdev_get_timestamp(struct cxl_memdev *memdev)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc = 0;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_GET_TIME);
	if (!cmd)
		return -ENOMEM;
	rc = cxl_cmd_set_output_payload(cmd, NULL, sizeof(unsigned long));
	if (rc) {
		err(ctx, "%s: cmd setup failed: %s\n",
		    cxl_memdev_get_devname(memdev), strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n", devname,
		    strerror(-rc));
		goto out;
	}
	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n", devname, rc);
		rc = -ENXIO;
		goto out;
	}
	rc = cxl_cmd_get_timestamp_get_payload(cmd);
out:
	cxl_cmd_unref(cmd);
	return rc;
}

CXL_EXPORT int cxl_memdev_get_shutdown_state(struct cxl_memdev *memdev)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc = 0;

	cmd = cxl_cmd_new_generic(memdev,
				  CXL_MEM_COMMAND_ID_GET_SHUTDOWN_STATE);
	if (!cmd)
		return -ENOMEM;
	rc = cxl_cmd_set_output_payload(cmd, NULL, sizeof(unsigned long));
	if (rc) {
		err(ctx, "%s: cmd setup failed: %s\n",
		    cxl_memdev_get_devname(memdev), strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n", devname,
		    strerror(-rc));
		goto out;
	}
	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n", devname, rc);
		rc = -ENXIO;
		goto out;
	}
	rc = cxl_cmd_get_shutdown_state_payload(cmd);
out:
	cxl_cmd_unref(cmd);
	return rc;
}

CXL_EXPORT int cxl_memdev_set_shutdown_state(struct cxl_memdev *memdev,
					     bool is_clean)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc = 0;

	cmd = cxl_cmd_new_set_shutdown_state(memdev, is_clean);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n", devname,
		    strerror(-rc));
		goto out;
	}
	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n", devname, rc);
		rc = -ENXIO;
	}

out:
	cxl_cmd_unref(cmd);
	return rc;
}

CXL_EXPORT int cxl_memdev_sanitize(struct cxl_memdev *memdev, const char *op)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc = 0;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_SANITIZE);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n", devname,
		    strerror(-rc));
		goto out;
	}
	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n", devname, rc);
		rc = -ENXIO;
	}
out:
	cxl_cmd_unref(cmd);
	return rc;
}

static char *event_flag_type(unsigned int flag)
{
	char *type = "";

	switch (flag) {
	case 0:
		type = "Informational Event";
		break;
	case 1:
		type = "Warning Event";
		break;
	case 2:
		type = "Failure Event";
		break;
	case 3:
		type = "Fatal Event";
		break;
	case 4:
		type = "Permanent Condition";
		break;
	case 8:
		type = "Maintenance Needed";
		break;
	case 16:
		type = "Performance Degraded";
		break;
	case 32:
		type = "Hardware Replacement Needed ";
		break;
	default:
		type = "Unknown";
	}

	return type;
}

static char *mevent_desc(unsigned int flag)
{
	char *type = "";

	switch (flag) {
	case 1:
		type = "Uncorrectable Event";
		break;
	case 2:
		type = "Threshold Event";
		break;
	case 4:
		type = "Poison List Overflow Event";
		break;
	default:
		type = "Unknown";
	}

	return type;
}

static char *mevent_type(unsigned int flag)
{
	char *type = "";

	switch (flag) {
	case 0:
		type = "Media ECC Error";
		break;
	case 1:
		type = "Invalid Address";
		break;
	case 2:
		type = "Data Path Error";
		break;
	default:
		type = "Unknown";
	}

	return type;
}

static char *trans_type(unsigned int flag)
{
	char *type = "";

	switch (flag) {
	case 1:
		type = "Host Read";
		break;
	case 2:
		type = "Host Write";
		break;
	case 3:
		type = "Host Scan Media";
		break;
	case 4:
		type = "Host Inject Poison";
		break;
	case 5:
		type = "Internal Media Scrub";
		break;
	case 6:
		type = "Internal Media Management";
		break;
	default:
		type = "Unknown / Unreported";
	}

	return type;
}

static char *devent_type(unsigned int flag)
{
	char *type = "";

	switch (flag) {
	case 0:
		type = "Health Status Change";
		break;
	case 1:
		type = "Media Status Change";
		break;
	case 2:
		type = "Life Used Change";
		break;
	case 3:
		type = "Temperature Change";
		break;
	case 4:
		type = "Data Path Error";
		break;
	case 5:
		type = "LSA Error";
		break;
	}

	return type;
}

CXL_EXPORT void cxl_memdev_print_get_health_info(struct cxl_memdev *memdev,
						 struct cxl_cmd *cmd)
{
	int life_used, temperature, volatile_err, persistent_err;

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
}

static void print_eventlog(struct cxl_memdev *memdev,
			   struct cxl_get_event_payload *ret)
{
	unsigned int count = ret->record_count;
	unsigned int *uuid, i;
	struct cxl_event_record_hdr *hdr;
	struct media_event *data_me;
	struct dram_event *data_de;
	struct memory_module_event *data_mme;
	struct cxl_cmd *cmd;

	printf("\n Received %u event records from device\n", count);

	for (i = 0; i < count; i++) {
		hdr = &(ret->records[i].hdr);
		uuid = (unsigned int *)hdr->id;

		printf("No. %u\n", i + 1);
		printf("UUID                             : %x-%x-%x-%x",
		       uuid[3], uuid[2], uuid[1], uuid[0]);

		switch (uuid[3]) {
		case 0xfbcd0a77: /*general media event*/
			data_me = (struct media_event *)&(ret->records[i].data);
			printf(" (General Media Event)\n");
			printf("Physical address                 : 0x%lx\n",
			       data_me->physical_address);
			printf("Memory Event Desc                : %s \n",
			       mevent_desc(data_me->memory_event_desc));
			/* TODO: why print desc '4' as '2'? */
			printf("Memory Event Type                : %s \n",
			       mevent_type(data_me->memory_event_type));
			printf("Transaction Type                 : %s \n",
			       trans_type(data_me->transaction_type));
			break;
		case 0x601dcbb3: /*dram event*/
			data_de = (struct dram_event *)&(ret->records[i].data);
			printf(" (DRAM Event)\n");
			printf("Physical address                 : 0x%lx\n",
			       data_de->physical_address);
			printf("Memory Event Desc                : %s \n",
			       mevent_desc(data_de->memory_event_desc));
			printf("Memory Event Type                : %s \n",
			       mevent_type(data_de->memory_event_type));
			printf("Transaction Type                 : %s \n",
			       trans_type(data_de->transaction_type));
			/* TODO: Implement more filed in CXL 2.0 8.2.9.1.1.2 */
			break;
		case 0xfe927475: /*memory module event*/
			data_mme = (struct memory_module_event *)&(
				ret->records[i].data);
			/* Set dummy commands */
			cmd = calloc(1, sizeof(struct cxl_cmd));
			cmd->send_cmd =
				calloc(1, sizeof(struct cxl_send_command));
			/* todo: null check */
			cmd->status = 0;
			cmd->send_cmd->id = CXL_MEM_COMMAND_ID_GET_HEALTH_INFO;
			cmd->send_cmd->out.payload =
				(u64)data_mme->device_health_info;
			printf(" (Memory Module Event)\n");
			printf("Device Event Type                : %s \n",
			       devent_type(data_mme->device_event_type));
			cxl_memdev_print_get_health_info(memdev, cmd);
			break;
		default: /*vendor event or invalid uuid*/
			printf(" (Invalid UUID)\n");
			break;
		}

		printf("Event Record Flags               : %s \n",
		       event_flag_type(hdr->flags[0]));
		printf("Event Timestamp                  : ");
		print_timestamp(hdr->timestamp);
		printf("Handle                           : %u\n", hdr->handle);
		printf("\n");
	}
	printf("Overflow Error Count             : %hd\n",
	       ret->overflow_err_count);
	if (ret->overflow_err_count) {
		printf("First Overflow Event Timestamp   : ");
		print_timestamp(ret->first_overflow_timestamp);
		printf("Last Overflow Event Timestamp    : ");
		print_timestamp(ret->last_overflow_timestamp);
	}
}

static void handle_eventlog(struct cxl_memdev *memdev,
			    struct cxl_get_event_payload *ret, int event_type)
{
	u8 more = ret->flags & EVENT_MORE_RECORD;

	print_eventlog(memdev, ret);
	if (more)
		cxl_memdev_get_event_record(memdev, event_type);
}

CXL_EXPORT ssize_t cxl_cmd_get_event_record_get_payload(
	struct cxl_cmd *cmd, struct cxl_memdev *memdev, int event_type)
{
	int rc = 0;
	struct cxl_get_event_payload *ret;
	rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_EVENT_LOG);
	if (rc)
		return rc;

	ret = (struct cxl_get_event_payload *)cmd->send_cmd->out.payload;
	if (!ret)
		return -ENXIO;
	handle_eventlog(memdev, ret, event_type);
	return 1;
}

CXL_EXPORT struct cxl_cmd *
cxl_cmd_new_get_event_record(struct cxl_memdev *memdev, int event_type)
{
	u8 *get_event;
	struct cxl_cmd *cmd;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_GET_EVENT_LOG);
	if (!cmd)
		return NULL;

	get_event = (u8 *)cmd->send_cmd->in.payload;
	*get_event = (u8)event_type;
	return cmd;
}

CXL_EXPORT int cxl_memdev_get_event_record(struct cxl_memdev *memdev,
					   int event_type)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc = 0;
	ssize_t ret_len;

	cmd = cxl_cmd_new_get_event_record(memdev, event_type);
	if (!cmd)
		return -ENOMEM;
	rc = cxl_cmd_set_output_payload(cmd, NULL, memdev->payload_max);
	if (rc) {
		err(ctx, "%s: cmd setup failed: %s\n",
		    cxl_memdev_get_devname(memdev), strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n", devname,
		    strerror(-rc));
		goto out;
	}

	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n", devname, rc);
		rc = -ENXIO;
		goto out;
	}

	ret_len = cxl_cmd_get_event_record_get_payload(cmd, memdev, event_type);
	if (ret_len < 0) {
		rc = ret_len;
		goto out;
	}
out:
	cxl_cmd_unref(cmd);
	return rc;
}

CXL_EXPORT struct cxl_cmd *
cxl_cmd_new_clear_event_record(struct cxl_memdev *memdev, int type,
			       bool clear_all, int handle)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd_clear_event_record_in *clear_event;
	struct cxl_cmd *cmd;
	int rc;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_CLEAR_EVENT);
	if (!cmd)
		return NULL;

	rc = cxl_cmd_set_input_payload(cmd, NULL, sizeof(*clear_event));
	if (rc) {
		err(ctx, "%s: cmd setup failed: %s\n",
		    cxl_memdev_get_devname(memdev), strerror(-rc));
		goto out_fail;
	}

	clear_event = (struct cxl_cmd_clear_event_record_in *)
			      cmd->send_cmd->in.payload;
	clear_event->event_type = (u8)type;
	if (clear_all) {
		clear_event->flags = (u8)1;
		return cmd;
	}
	clear_event->n_event_handle = 1;
	clear_event->event_record_handle = cpu_to_le16(handle);
	return cmd;

out_fail:
	cxl_cmd_unref(cmd);
	return NULL;
}

CXL_EXPORT int cxl_memdev_clear_event_record(struct cxl_memdev *memdev,
					     int type, bool clear_all,
					     int handle)
{
	const char *devname = cxl_memdev_get_devname(memdev);
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd *cmd;
	int rc = 0;

	cmd = cxl_cmd_new_clear_event_record(memdev, type, clear_all, handle);
	if (!cmd)
		return -ENOMEM;

	rc = cxl_cmd_submit(cmd);
	if (rc < 0) {
		err(ctx, "%s: cmd submission failed: %s\n", devname,
		    strerror(-rc));
		goto out;
	}
	rc = cxl_cmd_get_mbox_status(cmd);
	if (rc != 0) {
		err(ctx, "%s: firmware status: %d\n", devname, rc);
		rc = -ENXIO;
	}
out:
	cxl_cmd_unref(cmd);
	return rc;
}

CXL_EXPORT int
cxl_cmd_identify_get_event_log_size(struct cxl_cmd *cmd,
				    enum cxl_identify_event event)
{
	struct cxl_cmd_identify *id =
		(struct cxl_cmd_identify *)cmd->send_cmd->out.payload;
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_IDENTIFY);
	if (rc)
		return rc;

	switch (event) {
	case CXL_IDENTIFY_INFO:
		return le16_to_cpu(id->info_event_log_size);
	case CXL_IDENTIFY_WARN:
		return le16_to_cpu(id->warning_event_log_size);
	case CXL_IDENTIFY_FAIL:
		return le16_to_cpu(id->failure_event_log_size);
	case CXL_IDENTIFY_FATAL:
		return le16_to_cpu(id->fatal_event_log_size);
	default:
		return -EINVAL;
	}
}

CXL_EXPORT int cxl_cmd_identify_get_poison_list_max(struct cxl_cmd *cmd)
{
	unsigned int max_records = 0;
	struct cxl_cmd_identify *id =
		(struct cxl_cmd_identify *)cmd->send_cmd->out.payload;
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_IDENTIFY);
	if (rc)
		return rc;

	for (int i = 0; i < 3; i++)
		max_records += id->poison_list_max_mer[i] << (8 * i);

	return max_records;
}

CXL_EXPORT int cxl_cmd_identify_get_inject_poison_limit(struct cxl_cmd *cmd)
{
	cmd_get_field_u16(cmd, identify, IDENTIFY, inject_poison_limit);
}

CXL_EXPORT int cxl_cmd_identify_injects_persistent_poison(struct cxl_cmd *cmd)
{
	cmd_get_field_u8_mask(
		cmd, identify, IDENTIFY, poison_caps,
		CXL_CMD_IDENTIFY_POISON_HANDLING_CAPABILITIES_INJECTS_PERSISTENT_POISON_MASK);
}

CXL_EXPORT int cxl_cmd_identify_scans_for_poison(struct cxl_cmd *cmd)
{
	cmd_get_field_u8_mask(
		cmd, identify, IDENTIFY, poison_caps,
		CXL_CMD_IDENTIFY_POISON_HANDLING_CAPABILITIES_SCANS_FOR_POISON_MASK);
}

CXL_EXPORT int cxl_cmd_identify_egress_port_congestion(struct cxl_cmd *cmd)
{
	cmd_get_field_u8_mask(
		cmd, identify, IDENTIFY, qos_telemetry_caps,
		CXL_CMD_IDENTIFY_QOS_TELEMETRY_CAPABILITIES_EGRESS_PORT_CONGESTION_MASK);
}

CXL_EXPORT int
cxl_cmd_identify_temporary_throughput_reduction(struct cxl_cmd *cmd)
{
	cmd_get_field_u8_mask(
		cmd, identify, IDENTIFY, qos_telemetry_caps,
		CXL_CMD_IDENTIFY_QOS_TELEMETRY_CAPABILITIES_TEMPORARY_THROUGHPUT_REDUCTION_MASK);
}

CXL_EXPORT struct cxl_cmd *
cxl_cmd_new_set_alert_config(struct cxl_memdev *memdev,
			     enum cxl_setalert_event event, int enable,
			     int threshold)
{
	struct cxl_cmd_set_alert_config *setalert;
	struct cxl_cmd *cmd;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_SET_ALERT_CONFIG);
	if (!cmd)
		return NULL;

	setalert = cmd->input_payload;

	setalert->valid_alert_actions = 1 << event;
	if (enable) {
		setalert->enable_alert_actions = 1 << event;
		if (event == CXL_SETALERT_LIFE)
			setalert->life_used_prog_warn_threshold = (u8)threshold;
		else if (event == CXL_SETALERT_OVER_TEMP)
			setalert->dev_over_temperature_prog_warn_threshold =
				cpu_to_le16(threshold);
		else if (event == CXL_SETALERT_UNDER_TEMP)
			setalert->dev_under_temperature_prog_warn_threshold =
				cpu_to_le16(threshold);
		else if (event == CXL_SETALERT_VOLATILE_ERROR)
			setalert->corrected_volatile_mem_err_prog_warn_threshold =
				cpu_to_le16(threshold);
		else if (event == CXL_SETALERT_PMEM_ERROR)
			setalert->corrected_pmem_err_prog_warn_threshold =
				cpu_to_le16(threshold);
		else {
			cxl_cmd_unref(cmd);
			return NULL;
		}
	}

	return cmd;
}

CXL_EXPORT struct cxl_cmd *
cxl_cmd_new_get_firmware_info(struct cxl_memdev *memdev)
{
	return cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_GET_FW_INFO);
}

CXL_EXPORT int cxl_cmd_firmware_info_get_slots_supported(struct cxl_cmd *cmd)
{
	cmd_get_field_u8(cmd, get_firmware_info, GET_FW_INFO, slots_supported);
}

CXL_EXPORT int cxl_cmd_firmware_info_get_active_slot(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_firmware_info *fw =
		(struct cxl_cmd_get_firmware_info *)cmd->send_cmd->out.payload;
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_FW_INFO);
	if (rc)
		return rc;

	return FIELD_GET(CXL_CMD_FW_INFO_SLOT_ACTIVE_MASK, fw->slot_info);
}

CXL_EXPORT int cxl_cmd_firmware_info_get_staged_slot(struct cxl_cmd *cmd)
{
	struct cxl_cmd_get_firmware_info *fw =
		(struct cxl_cmd_get_firmware_info *)cmd->send_cmd->out.payload;
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_FW_INFO);
	if (rc)
		return rc;

	return FIELD_GET(CXL_CMD_FW_INFO_SLOT_STAGED_MASK, fw->slot_info);
}

CXL_EXPORT int
cxl_cmd_firmware_info_online_activation_capable(struct cxl_cmd *cmd)
{
	cmd_get_field_u8_mask(
		cmd, get_firmware_info, GET_FW_INFO, activation_caps,
		CXL_CMD_FW_INFO_ACTIVATION_CAPABILITIES_ONLINE_FW_ACTIVATION_MASK);
}

CXL_EXPORT int cxl_cmd_firmware_info_get_fw_rev(struct cxl_cmd *cmd,
						char *fw_rev, int fw_len,
						int slot)
{
	struct cxl_cmd_get_firmware_info *fw =
		(struct cxl_cmd_get_firmware_info *)cmd->send_cmd->out.payload;
	int rc = cxl_cmd_validate_status(cmd, CXL_MEM_COMMAND_ID_GET_FW_INFO);
	if (rc)
		return rc;

	if (slot <= 0 || slot > fw->slots_supported)
		return -EINVAL;
	if (fw_len > 0)
		memcpy(fw_rev, fw->fw_revisions[slot - 1],
		       min(fw_len, CXL_CMD_FW_INFO_FW_REV_LENGTH));
	return 0;
}

CXL_EXPORT struct cxl_cmd *cxl_cmd_new_transfer_firmware(
	struct cxl_memdev *memdev, enum cxl_transfer_fw_action action, int slot,
	unsigned int offset, void *fw_buf, unsigned int length)
{
	struct cxl_ctx *ctx = cxl_memdev_get_ctx(memdev);
	struct cxl_cmd_transfer_firmware *transfer_fw;
	struct cxl_cmd *cmd;
	int rc;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_TRANSFER_FW);
	if (!cmd)
		return NULL;

	rc = cxl_cmd_set_input_payload(cmd, NULL,
				       sizeof(*transfer_fw) + length);
	if (rc) {
		err(ctx, "%s: cmd setup failed: %s\n",
		    cxl_memdev_get_devname(memdev), strerror(-rc));
		cxl_cmd_unref(cmd);
		return NULL;
	}
	transfer_fw =
		(struct cxl_cmd_transfer_firmware *)cmd->send_cmd->in.payload;
	transfer_fw->action = (u8)action;
	if (length > 0) {
		transfer_fw->slot = (u8)slot;
		transfer_fw->offset = cpu_to_le32(offset);
		memcpy(transfer_fw->fw_data, fw_buf, length);
	}

	return cmd;
}

CXL_EXPORT struct cxl_cmd *
cxl_cmd_new_activate_firmware(struct cxl_memdev *memdev, bool online, int slot)
{
	struct cxl_cmd_activate_firmware *activate_fw;
	struct cxl_cmd *cmd;

	cmd = cxl_cmd_new_generic(memdev, CXL_MEM_COMMAND_ID_ACTIVATE_FW);
	if (!cmd)
		return NULL;

	activate_fw =
		(struct cxl_cmd_activate_firmware *)cmd->send_cmd->in.payload;
	activate_fw->action = !online;
	activate_fw->slot = (u8)slot;

	return cmd;
}

CXL_EXPORT struct cxl_cmd *
cxl_cmd_new_set_shutdown_state(struct cxl_memdev *memdev, bool is_clean)
{
	struct cxl_cmd *cmd;
	bool *state;

	cmd = cxl_cmd_new_generic(memdev,
				  CXL_MEM_COMMAND_ID_SET_SHUTDOWN_STATE);
	if (!cmd)
		return NULL;

	state = cmd->input_payload;
	*state = !is_clean;

	return cmd;
}
