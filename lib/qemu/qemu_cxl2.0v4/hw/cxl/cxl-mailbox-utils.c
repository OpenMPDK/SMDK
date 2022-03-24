/*
 * CXL Utility library for mailbox interface
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/cxl/cxl.h"
#include "hw/pci/pci.h"
#include "qemu/log.h"
#include "qemu/uuid.h"

/*
 * How to add a new command, example. The command set FOO, with cmd BAR.
 *  1. Add the command set and cmd to the enum.
 *     FOO    = 0x7f,
 *          #define BAR 0
 *  2. Forward declare the handler.
 *     declare_mailbox_handler(FOO_BAR);
 *  3. Add the command to the cxl_cmd_set[][]
 *     CXL_CMD(FOO, BAR, 0, 0),
 *  4. Implement your handler
 *     define_mailbox_handler(FOO_BAR) { ... return CXL_MBOX_SUCCESS; }
 *
 *
 *  Writing the handler:
 *    The handler will provide the &struct cxl_cmd, the &CXLDeviceState, and the
 *    in/out length of the payload. The handler is responsible for consuming the
 *    payload from cmd->payload and operating upon it as necessary. It must then
 *    fill the output data into cmd->payload (overwriting what was there),
 *    setting the length, and returning a valid return code.
 *
 *  XXX: The handler need not worry about endianess. The payload is read out of
 *  a register interface that already deals with it.
 */

enum {
    EVENTS      = 0x01,
        #define GET_RECORDS   0x0
        #define CLEAR_RECORDS   0x1
        #define GET_INTERRUPT_POLICY   0x2
        #define SET_INTERRUPT_POLICY   0x3
    FIRMWARE_UPDATE = 0x02,
        #define GET_INFO      0x0
    TIMESTAMP   = 0x03,
        #define GET           0x0
        #define SET           0x1
    LOGS        = 0x04,
        #define GET_SUPPORTED 0x0
        #define GET_LOG       0x1
    IDENTIFY    = 0x40,
        #define MEMORY_DEVICE 0x0
    CCLS        = 0x41,
        #define GET_PARTITION_INFO     0x0
        #define GET_LSA       0x2
        #define SET_LSA       0x3
};

/* 8.2.8.4.5.1 Command Return Codes */
typedef enum {
    CXL_MBOX_SUCCESS = 0x0,
    CXL_MBOX_BG_STARTED = 0x1,
    CXL_MBOX_INVALID_INPUT = 0x2,
    CXL_MBOX_UNSUPPORTED = 0x3,
    CXL_MBOX_INTERNAL_ERROR = 0x4,
    CXL_MBOX_RETRY_REQUIRED = 0x5,
    CXL_MBOX_BUSY = 0x6,
    CXL_MBOX_MEDIA_DISABLED = 0x7,
    CXL_MBOX_FW_XFER_IN_PROGRESS = 0x8,
    CXL_MBOX_FW_XFER_OUT_OF_ORDER = 0x9,
    CXL_MBOX_FW_AUTH_FAILED = 0xa,
    CXL_MBOX_FW_INVALID_SLOT = 0xb,
    CXL_MBOX_FW_ROLLEDBACK = 0xc,
    CXL_MBOX_FW_REST_REQD = 0xd,
    CXL_MBOX_INVALID_HANDLE = 0xe,
    CXL_MBOX_INVALID_PA = 0xf,
    CXL_MBOX_INJECT_POISON_LIMIT = 0x10,
    CXL_MBOX_PERMANENT_MEDIA_FAILURE = 0x11,
    CXL_MBOX_ABORTED = 0x12,
    CXL_MBOX_INVALID_SECURITY_STATE = 0x13,
    CXL_MBOX_INCORRECT_PASSPHRASE = 0x14,
    CXL_MBOX_UNSUPPORTED_MAILBOX = 0x15,
    CXL_MBOX_INVALID_PAYLOAD_LENGTH = 0x16,
    CXL_MBOX_MAX = 0x17
} ret_code;

struct cxl_cmd;
typedef ret_code (*opcode_handler)(struct cxl_cmd *cmd,
                                   CXLDeviceState *cxl_dstate, uint16_t *len);
struct cxl_cmd {
    const char *name;
    opcode_handler handler;
    ssize_t in;
    uint16_t effect; /* Reported in CEL */
    uint8_t *payload;
};

#define define_mailbox_handler(name)                \
    static ret_code cmd_##name(struct cxl_cmd *cmd, \
                               CXLDeviceState *cxl_dstate, uint16_t *len)
#define declare_mailbox_handler(name) define_mailbox_handler(name)

#define define_mailbox_handler_zeroed(name, size)                         \
    uint16_t __zero##name = size;                                         \
    static ret_code cmd_##name(struct cxl_cmd *cmd,                       \
                               CXLDeviceState *cxl_dstate, uint16_t *len) \
    {                                                                     \
        *len = __zero##name;                                              \
        memset(cmd->payload, 0, *len);                                    \
        return CXL_MBOX_SUCCESS;                                          \
    }
#define define_mailbox_handler_const(name, data)                          \
    static ret_code cmd_##name(struct cxl_cmd *cmd,                       \
                               CXLDeviceState *cxl_dstate, uint16_t *len) \
    {                                                                     \
        *len = sizeof(data);                                              \
        memcpy(cmd->payload, data, *len);                                 \
        return CXL_MBOX_SUCCESS;                                          \
    }
#define define_mailbox_handler_nop(name)                                  \
    static ret_code cmd_##name(struct cxl_cmd *cmd,                       \
                               CXLDeviceState *cxl_dstate, uint16_t *len) \
    {                                                                     \
        return CXL_MBOX_SUCCESS;                                          \
    }

define_mailbox_handler_zeroed(EVENTS_GET_RECORDS, 0x20);
define_mailbox_handler_nop(EVENTS_CLEAR_RECORDS);
define_mailbox_handler_zeroed(EVENTS_GET_INTERRUPT_POLICY, 4);
define_mailbox_handler_nop(EVENTS_SET_INTERRUPT_POLICY);
declare_mailbox_handler(FIRMWARE_UPDATE_GET_INFO);
declare_mailbox_handler(TIMESTAMP_GET);
declare_mailbox_handler(TIMESTAMP_SET);
declare_mailbox_handler(LOGS_GET_SUPPORTED);
declare_mailbox_handler(LOGS_GET_LOG);
declare_mailbox_handler(IDENTIFY_MEMORY_DEVICE);
declare_mailbox_handler(CCLS_GET_PARTITION_INFO);
declare_mailbox_handler(CCLS_GET_LSA);
declare_mailbox_handler(CCLS_SET_LSA);

#define IMMEDIATE_CONFIG_CHANGE (1 << 1)
#define IMMEDIATE_DATA_CHANGE (1 << 1)
#define IMMEDIATE_POLICY_CHANGE (1 << 3)
#define IMMEDIATE_LOG_CHANGE (1 << 4)

#define CXL_CMD(s, c, in, cel_effect) \
    [s][c] = { stringify(s##_##c), cmd_##s##_##c, in, cel_effect }

static struct cxl_cmd cxl_cmd_set[256][256] = {
    CXL_CMD(EVENTS, GET_RECORDS, 1, 0),
    CXL_CMD(EVENTS, CLEAR_RECORDS, ~0, IMMEDIATE_LOG_CHANGE),
    CXL_CMD(EVENTS, GET_INTERRUPT_POLICY, 0, 0),
    CXL_CMD(EVENTS, SET_INTERRUPT_POLICY, 4, IMMEDIATE_CONFIG_CHANGE),
    CXL_CMD(FIRMWARE_UPDATE, GET_INFO, 0, 0),
    CXL_CMD(TIMESTAMP, GET, 0, 0),
    CXL_CMD(TIMESTAMP, SET, 8, IMMEDIATE_POLICY_CHANGE),
    CXL_CMD(LOGS, GET_SUPPORTED, 0, 0),
    CXL_CMD(LOGS, GET_LOG, 0x18, 0),
    CXL_CMD(IDENTIFY, MEMORY_DEVICE, 0, 0),
    CXL_CMD(CCLS, GET_PARTITION_INFO, 0, 0),
    CXL_CMD(CCLS, GET_LSA, 0, 0),
    CXL_CMD(CCLS, SET_LSA, ~0, IMMEDIATE_CONFIG_CHANGE | IMMEDIATE_DATA_CHANGE),
};

#undef CXL_CMD

/*
 * 8.2.9.2.1
 */
define_mailbox_handler(FIRMWARE_UPDATE_GET_INFO)
{
    struct {
        uint8_t slots_supported;
        uint8_t slot_info;
        uint8_t caps;
        uint8_t rsvd[0xd];
        char fw_rev1[0x10];
        char fw_rev2[0x10];
        char fw_rev3[0x10];
        char fw_rev4[0x10];
    } __attribute__((packed)) *fw_info;
    _Static_assert(sizeof(*fw_info) == 0x50, "Bad firmware info size");

    if (memory_region_size(cxl_dstate->pmem) < (256 << 20)) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    fw_info = (void *)cmd->payload;
    memset(fw_info, 0, sizeof(*fw_info));

    fw_info->slots_supported = 2;
    fw_info->slot_info = BIT(0) | BIT(3);
    fw_info->caps = 0;
    snprintf(fw_info->fw_rev1, 0x10, "BWFW VERSION %02d", 0);

    *len = sizeof(*fw_info);
    return CXL_MBOX_SUCCESS;
}

/*
 * 8.2.9.3.1
 */
define_mailbox_handler(TIMESTAMP_GET)
{
    struct timespec ts;
    uint64_t delta;

    if (!cxl_dstate->timestamp.set) {
        *(uint64_t *)cmd->payload = 0;
        goto done;
    }

    /* First find the delta from the last time the host set the time. */
    clock_gettime(CLOCK_REALTIME, &ts);
    delta = (ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec) -
            cxl_dstate->timestamp.last_set;

    /* Then adjust the actual time */
    stq_le_p(cmd->payload, cxl_dstate->timestamp.host_set + delta);

done:
    *len = 8;
    return CXL_MBOX_SUCCESS;
}

/*
 * 8.2.9.3.2
 */
define_mailbox_handler(TIMESTAMP_SET)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);

    cxl_dstate->timestamp.set = true;
    cxl_dstate->timestamp.last_set =
        ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;

    cxl_dstate->timestamp.host_set = le64_to_cpu(*(uint64_t *)cmd->payload);

    *len = 0;
    return CXL_MBOX_SUCCESS;
}

QemuUUID cel_uuid;

/* 8.2.9.4.1 */
define_mailbox_handler(LOGS_GET_SUPPORTED)
{
    struct {
        uint16_t entries;
        uint8_t rsvd[6];
        struct {
            QemuUUID uuid;
            uint32_t size;
        } log_entries[1];
    } __attribute__((packed)) *supported_logs = (void *)cmd->payload;
    _Static_assert(sizeof(*supported_logs) == 0x1c, "Bad supported log size");

    supported_logs->entries = 1;
    supported_logs->log_entries[0].uuid = cel_uuid;
    supported_logs->log_entries[0].size = 4 * cxl_dstate->cel_size;

    *len = sizeof(*supported_logs);
    return CXL_MBOX_SUCCESS;
}

/* 8.2.9.4.2 */
define_mailbox_handler(LOGS_GET_LOG)
{
    struct {
        QemuUUID uuid;
        uint32_t offset;
        uint32_t length;
    } __attribute__((packed, __aligned__(16))) *get_log = (void *)cmd->payload;

    /*
     * 8.2.9.4.2
     *   The device shall return Invalid Parameter if the Offset or Length
     *   fields attempt to access beyond the size of the log as reported by Get
     *   Supported Logs.
     *
     * XXX: Spec is wrong, "Invalid Parameter" isn't a thing.
     * XXX: Spec doesn't address incorrect UUID incorrectness.
     *
     * The CEL buffer is large enough to fit all commands in the emulation, so
     * the only possible failure would be if the mailbox itself isn't big
     * enough.
     */
    if (get_log->offset + get_log->length > cxl_dstate->payload_size) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (!qemu_uuid_is_equal(&get_log->uuid, &cel_uuid)) {
        return CXL_MBOX_UNSUPPORTED;
    }

    /* Store off everything to local variables so we can wipe out the payload */
    *len = get_log->length;

    memmove(cmd->payload, cxl_dstate->cel_log + get_log->offset,
           get_log->length);

    return CXL_MBOX_SUCCESS;
}

/* 8.2.9.5.1.1 */
define_mailbox_handler(IDENTIFY_MEMORY_DEVICE)
{
    struct {
        char fw_revision[0x10];
        uint64_t total_capacity;
        uint64_t volatile_capacity;
        uint64_t persistent_capacity;
        uint64_t partition_align;
        uint16_t info_event_log_size;
        uint16_t warning_event_log_size;
        uint16_t failure_event_log_size;
        uint16_t fatal_event_log_size;
        uint32_t lsa_size;
        uint8_t poison_list_max_mer[3];
        uint16_t inject_poison_limit;
        uint8_t poison_caps;
        uint8_t qos_telemetry_caps;
    } __attribute__((packed)) *id;
    _Static_assert(sizeof(*id) == 0x43, "Bad identify size");

    CXLType3Dev *ct3d = container_of(cxl_dstate, CXLType3Dev, cxl_dstate);
    CXLType3Class *cvc = CXL_TYPE3_DEV_GET_CLASS(ct3d);

    if (memory_region_size(cxl_dstate->pmem) < (256 << 20)) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    id = (void *)cmd->payload;
    memset(id, 0, sizeof(*id));

    /* PMEM only */
    snprintf(id->fw_revision, 0x10, "BWFW VERSION %02d", 0);
    id->total_capacity = memory_region_size(cxl_dstate->pmem);
    id->persistent_capacity = memory_region_size(cxl_dstate->pmem);
    id->lsa_size = cvc->get_lsa_size(ct3d);

    *len = sizeof(*id);
    return CXL_MBOX_SUCCESS;
}

define_mailbox_handler(CCLS_GET_PARTITION_INFO)
{
    struct {
        uint64_t active_vmem;
        uint64_t active_pmem;
        uint64_t next_vmem;
        uint64_t next_pmem;
    } __attribute__((packed)) *part_info = (void *)cmd->payload;
    _Static_assert(sizeof(*part_info) == 0x20, "Bad get partition info size");

    if (memory_region_size(cxl_dstate->pmem) < (256 << 20)) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    /* PMEM only */
    part_info->active_vmem = 0;
    part_info->next_vmem = 0;
    part_info->active_pmem = memory_region_size(cxl_dstate->pmem);
    part_info->next_pmem = part_info->active_pmem;

    *len = sizeof(*part_info);
    return CXL_MBOX_SUCCESS;
}

define_mailbox_handler(CCLS_GET_LSA)
{
    struct {
        uint32_t offset;
        uint32_t length;
    } __attribute__((packed, __aligned__(8))) *get_lsa;
    CXLType3Dev *ct3d = container_of(cxl_dstate, CXLType3Dev, cxl_dstate);
    CXLType3Class *cvc = CXL_TYPE3_DEV_GET_CLASS(ct3d);
    uint32_t offset, length;

    get_lsa = (void *)cmd->payload;
    offset = get_lsa->offset;
    length = get_lsa->length;

    *len = 0;
    if (offset + length > cvc->get_lsa_size(ct3d)) {
        return CXL_MBOX_INVALID_INPUT;
    }

    *len = cvc->get_lsa(ct3d, get_lsa, length, offset);
    return CXL_MBOX_SUCCESS;
}

define_mailbox_handler(CCLS_SET_LSA)
{
    struct {
        uint32_t offset;
        uint32_t rsvd;
    } __attribute__((packed, __aligned__(8))) *set_lsa = (void *)cmd->payload;
    CXLType3Dev *ct3d = container_of(cxl_dstate, CXLType3Dev, cxl_dstate);
    CXLType3Class *cvc = CXL_TYPE3_DEV_GET_CLASS(ct3d);
    uint16_t plen = *len;

    *len = 0;
    if (!plen) {
        return CXL_MBOX_SUCCESS;
    }

    if (set_lsa->offset + plen > cvc->get_lsa_size(ct3d) + sizeof(*set_lsa)) {
        return CXL_MBOX_INVALID_INPUT;
    }

    cvc->set_lsa(ct3d, (void *)set_lsa + sizeof(*set_lsa),
                 plen - sizeof(*set_lsa), set_lsa->offset);
    return CXL_MBOX_SUCCESS;
}

void cxl_process_mailbox(CXLDeviceState *cxl_dstate)
{
    uint16_t ret = CXL_MBOX_SUCCESS;
    struct cxl_cmd *cxl_cmd;
    uint64_t status_reg;
    opcode_handler h;

    /*
     * current state of mailbox interface
     *  mbox_cap_reg = cxl_dstate->reg_state32[R_CXL_DEV_MAILBOX_CAP];
     *  mbox_ctrl_reg = cxl_dstate->reg_state32[R_CXL_DEV_MAILBOX_CTRL];
     *  status_reg = *(uint64_t *)&cxl_dstate->reg_state[A_CXL_DEV_MAILBOX_STS];
     */
    uint64_t command_reg =
        *(uint64_t *)&cxl_dstate->mbox_reg_state[A_CXL_DEV_MAILBOX_CMD];

    /* Check if we have to do anything */
    if (!ARRAY_FIELD_EX32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CTRL,
                          DOORBELL)) {
        qemu_log_mask(LOG_UNIMP, "Corrupt internal state for firmware\n");
        return;
    }

    uint8_t set = FIELD_EX64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND_SET);
    uint8_t cmd = FIELD_EX64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND);
    uint16_t len = FIELD_EX64(command_reg, CXL_DEV_MAILBOX_CMD, LENGTH);
    cxl_cmd = &cxl_cmd_set[set][cmd];
    h = cxl_cmd->handler;
    if (!h) {
        qemu_log_mask(LOG_UNIMP, "Command %04xh not implemented\n",
                                 set << 8 | cmd);
        goto handled;
    }

    if (len != cxl_cmd->in) {
        ret = CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    cxl_cmd->payload = cxl_dstate->mbox_reg_state + A_CXL_DEV_CMD_PAYLOAD;
    ret = (*h)(cxl_cmd, cxl_dstate, &len);
    assert(len <= cxl_dstate->payload_size);

handled:
    /*
     * Set the return code
     * XXX: it's a 64b register, but we're not setting the vendor, so we can get
     * away with this
     */
    status_reg = FIELD_DP64(0, CXL_DEV_MAILBOX_STS, ERRNO, ret);

    /*
     * Set the return length
     */
    command_reg = FIELD_DP64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND_SET, 0);
    command_reg = FIELD_DP64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND, 0);
    command_reg = FIELD_DP64(command_reg, CXL_DEV_MAILBOX_CMD, LENGTH, len);

    cxl_dstate->mbox_reg_state64[A_CXL_DEV_MAILBOX_CMD / 8] = command_reg;
    cxl_dstate->mbox_reg_state64[A_CXL_DEV_MAILBOX_STS / 8] = status_reg;

    /* Tell the host we're done */
    ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CTRL,
                     DOORBELL, 0);
}

int cxl_initialize_mailbox(CXLDeviceState *cxl_dstate)
{
    const char *cel_uuidstr = "0da9c0b5-bf41-4b78-8f79-96b1623b3f17";

    for (int set = 0; set < 256; set++) {
        for (int cmd = 0; cmd < 256; cmd++) {
            if (cxl_cmd_set[set][cmd].handler) {
                struct cxl_cmd *c = &cxl_cmd_set[set][cmd];
                struct cel_log *log =
                    &cxl_dstate->cel_log[cxl_dstate->cel_size];

                log->opcode = (set << 8) | cmd;
                log->effect = c->effect;
                cxl_dstate->cel_size++;
            }
        }
    }

    return qemu_uuid_parse(cel_uuidstr, &cel_uuid);
}
