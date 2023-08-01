/* SPDX-License-Identifier: LGPL-2.1 */
/* Copyright (C) 2020-2021, Intel Corporation. All rights reserved. */
#ifndef _LIBCXL_PRIVATE_SMDK_H_
#define _LIBCXL_PRIVATE_SMDK_H_

#include "../private.h"

struct media_error_record {
	le64 dpa;
	le32 len;
	le32 rsvd;
} __attribute__((packed));

struct cxl_cmd_poison_get_list_in {
	le64 address;
	le64 address_length;
} __attribute__((packed));

struct cxl_cmd_poison_get_list {
	u8 flags;
	u8 rsvd;
	le64 overflow_timestamp;
	le16 count;
	u8 rsvd2[0x14];
	struct media_error_record rcd[];
} __attribute__((packed));

struct cxl_cmd_clear_poison_in {
	le64 address;
	unsigned int clear_data[16];
} __attribute__((packed));

struct cxl_cmd_clear_event_record_in {
	u8 event_type;
	u8 flags;
	u8 n_event_handle;
	u8 rsvd[3];
	le16 event_record_handle;
} __attribute__((packed));

struct media_event {
	le64 physical_address;
	u8 memory_event_desc;
	u8 memory_event_type;
	u8 transaction_type;
	le16 validity_flags;
	u8 channel;
	u8 rank;
	le32 device : 24;
	le64 component_identifier[2]; /*u8 comp_id[0x10] ?*/
	u8 rsvd[0x2E];
} __attribute__((packed));

struct dram_event {
	le64 physical_address;
	u8 memory_event_desc;
	u8 memory_event_type;
	u8 transaction_type;
	le16 validity_flags;
	u8 channel;
	u8 rank;
	le32 nibble_mask : 24;
	u8 bank_group;
	u8 bank;
	le32 row : 24;
	le16 column;
	le64 correction_mask[4];
	u8 rsvd[0x17];
} __attribute__((packed));

struct memory_module_event {
	u8 device_event_type;
	u8 device_health_info[0x12];
	u8 rsvd[0x3D];
} __attribute__((packed));

struct cxl_event_record_hdr {
	uuid_t id;
	u8 length;
	u8 flags[3];
	le16 handle;
	le16 related_handle;
	le64 timestamp;
	u8 reserved[0x10]; /* for CXL 2.0 */
} __attribute__((packed));

#define CXL_EVENT_RECORD_DATA_LENGTH (0x50)
struct cxl_event_record_raw {
	struct cxl_event_record_hdr hdr;
	u8 data[CXL_EVENT_RECORD_DATA_LENGTH];
} __attribute__((packed));

struct cxl_get_event_payload {
	u8 flags;
	u8 rsvd1;
	le16 overflow_err_count;
	le64 first_overflow_timestamp;
	le64 last_overflow_timestamp;
	le16 record_count;
	u8 rsvd2[10];
	struct cxl_event_record_raw records[];
} __attribute__((packed));

#define POISON_ADDR_MASK   0xFFFFFFFFFFFFFFF8
#define POISON_SOURCE_MASK 0x7
#ifndef BIT
#define BIT(_x) (1 << (_x))
#endif

/* CXL 3.0 8.2.9.8.1.1 Identify Memory Device Poison Handling Capabilities */
#define CXL_CMD_IDENTIFY_POISON_HANDLING_CAPABILITIES_INJECTS_PERSISTENT_POISON_MASK \
	BIT(0)
#define CXL_CMD_IDENTIFY_POISON_HANDLING_CAPABILITIES_SCANS_FOR_POISON_MASK \
	BIT(1)

/* CXL 3.0 8.2.9.8.1.1 Identify Memory Device QoS Telemetry Capabilities */
#define CXL_CMD_IDENTIFY_QOS_TELEMETRY_CAPABILITIES_EGRESS_PORT_CONGESTION_MASK \
	BIT(0)
#define CXL_CMD_IDENTIFY_QOS_TELEMETRY_CAPABILITIES_TEMPORARY_THROUGHPUT_REDUCTION_MASK \
	BIT(1)

/* CXL 3.0. 8.2.9.8.3.3 Set Alert Configuration */
struct cxl_cmd_set_alert_config {
	u8 valid_alert_actions;
	u8 enable_alert_actions;
	u8 life_used_prog_warn_threshold;
	u8 rsvd;
	le16 dev_over_temperature_prog_warn_threshold;
	le16 dev_under_temperature_prog_warn_threshold;
	le16 corrected_volatile_mem_err_prog_warn_threshold;
	le16 corrected_pmem_err_prog_warn_threshold;
} __attribute__((packed));

/* CXL 3.0 8.2.9.3.1 Get FW Info */
#define CXL_CMD_FW_INFO_FW_REV_LENGTH 0x10

struct cxl_cmd_get_firmware_info {
	u8 slots_supported;
	u8 slot_info;
	u8 activation_caps;
	u8 rsvd[13];
	char fw_revisions[4][CXL_CMD_FW_INFO_FW_REV_LENGTH];
} __attribute__((packed));

/* CXL 3.0 8.2.9.3.1 Get FW Info Byte 1 FW Slot Info */
#define CXL_CMD_FW_INFO_SLOT_ACTIVE_MASK GENMASK(2, 0)
#define CXL_CMD_FW_INFO_SLOT_STAGED_MASK GENMASK(5, 3)

/* CXL 3.0 8.2.9.3.1 Get FW Info Byte 2 FW Activation Capabilities */
#define CXL_CMD_FW_INFO_ACTIVATION_CAPABILITIES_ONLINE_FW_ACTIVATION_MASK BIT(0)

/* CXL 3.0 8.2.9.3.2 Transfer FW */
struct cxl_cmd_transfer_firmware {
	u8 action;
	u8 slot;
	le16 rsvd;
	le32 offset;
	u8 rsvd2[0x78];
	unsigned char fw_data[0];
} __attribute__((packed));

/* CXL 3.0 8.2.9.3.3 Activate FW */
struct cxl_cmd_activate_firmware {
	u8 action;
	u8 slot;
} __attribute__((packed));

/* CXL 3.0 8.2.9.8.4.4 Get Scan Media Capabilities */
struct cxl_cmd_get_scan_media_caps_in {
	le64 dpa;
	le64 len;
} __attribute__((packed));

/* CXL 3.0 8.2.9.8.4.5 Scan Media */
struct cxl_cmd_scan_media_in {
	le64 dpa;
	le64 len;
	u8 flag;
} __attribute__((packed));

/* CXL 3.0 8.2.9.8.4.6 Get Scan Media Result */
struct cxl_cmd_get_scan_media {
	le64 dpa_restart;
	le64 len_restart;
	u8 flag;
	u8 rsvd;
	le16 count;
	u8 rsvd2[0xc];
	struct media_error_record rcd[];
} __attribute__((packed));
#define CXL_CMD_GET_SCAN_MEDIA_FLAGS_MORE_MEDIA_ERROR_RECORDS BIT(0)
#define CXL_CMD_GET_SCAN_MEDIA_FLAGS_SCAN_STOPPED_PREMATURELY BIT(1)

#define RESULT_GET_SCAN_MEDIA_MORE_RECORDS	  (0x1)
#define RESULT_GET_SCAN_MEDIA_STOPPED_PREMATURELY (0x2)

#endif /* _LIBCXL_PRIVATE_SMDK_H_ */
