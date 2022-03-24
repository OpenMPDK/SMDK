/*
 * QEMU CXL Devices
 *
 * Copyright (c) 2020 Intel
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_DEVICE_H
#define CXL_DEVICE_H

#include "hw/register.h"

/*
 * The following is how a CXL device's MMIO space is laid out. The only
 * requirement from the spec is that the capabilities array and the capability
 * headers start at offset 0 and are contiguously packed. The headers themselves
 * provide offsets to the register fields. For this emulation, registers will
 * start at offset 0x80 (m == 0x80). No secondary mailbox is implemented which
 * means that n = m + sizeof(mailbox registers) + sizeof(device registers).
 *
 * This is roughly described in 8.2.8 Figure 138 of the CXL 2.0 spec.
 *
 *                       +---------------------------------+
 *                       |                                 |
 *                       |    Memory Device Registers      |
 *                       |                                 |
 * n + PAYLOAD_SIZE_MAX  -----------------------------------
 *                  ^    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |         Mailbox Payload         |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    -----------------------------------
 *                  |    |       Mailbox Registers         |
 *                  |    |                                 |
 *                  n    -----------------------------------
 *                  ^    |                                 |
 *                  |    |        Device Registers         |
 *                  |    |                                 |
 *                  m    ---------------------------------->
 *                  ^    |  Memory Device Capability Header|
 *                  |    -----------------------------------
 *                  |    |     Mailbox Capability Header   |
 *                  |    -------------- --------------------
 *                  |    |     Device Capability Header    |
 *                  |    -----------------------------------
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |      Device Cap Array[0..n]     |
 *                  |    |                                 |
 *                  |    |                                 |
 *                       |                                 |
 *                  0    +---------------------------------+
 *
 */

#define CXL_DEVICE_CAP_HDR1_OFFSET 0x10 /* Figure 138 */
#define CXL_DEVICE_CAP_REG_SIZE 0x10 /* 8.2.8.2 */
#define CXL_DEVICE_CAPS_MAX 4 /* 8.2.8.2.1 + 8.2.8.5 */
#define CXL_CAPS_SIZE \
    (CXL_DEVICE_CAP_REG_SIZE * (CXL_DEVICE_CAPS_MAX + 1)) /* +1 for header */

#define CXL_DEVICE_REGISTERS_OFFSET 0x80 /* Read comment above */
#define CXL_DEVICE_REGISTERS_LENGTH 0x8 /* 8.2.8.3.1 */

#define CXL_MAILBOX_REGISTERS_OFFSET \
    (CXL_DEVICE_REGISTERS_OFFSET + CXL_DEVICE_REGISTERS_LENGTH)
#define CXL_MAILBOX_REGISTERS_SIZE 0x20 /* 8.2.8.4, Figure 139 */
#define CXL_MAILBOX_PAYLOAD_SHIFT 11
#define CXL_MAILBOX_MAX_PAYLOAD_SIZE (1 << CXL_MAILBOX_PAYLOAD_SHIFT)
#define CXL_MAILBOX_REGISTERS_LENGTH \
    (CXL_MAILBOX_REGISTERS_SIZE + CXL_MAILBOX_MAX_PAYLOAD_SIZE)

#define CXL_MEMORY_DEVICE_REGISTERS_OFFSET \
    (CXL_MAILBOX_REGISTERS_OFFSET + CXL_MAILBOX_REGISTERS_LENGTH)
#define CXL_MEMORY_DEVICE_REGISTERS_LENGTH 0x8

#define CXL_MMIO_SIZE                                       \
    CXL_DEVICE_CAP_REG_SIZE + CXL_DEVICE_REGISTERS_LENGTH + \
        CXL_MAILBOX_REGISTERS_LENGTH + CXL_MEMORY_DEVICE_REGISTERS_LENGTH

typedef struct cxl_device_state {
    MemoryRegion device_registers;

    /* mmio for device capabilities array - 8.2.8.2 */
    MemoryRegion device;
    MemoryRegion memory_device;
    struct {
        MemoryRegion caps;
        uint32_t caps_reg_state32[CXL_CAPS_SIZE / 4];
    };

    /* mmio for the mailbox registers 8.2.8.4 */
    struct {
        MemoryRegion mailbox;
        uint16_t payload_size;
        union {
            uint8_t mbox_reg_state[CXL_MAILBOX_REGISTERS_LENGTH];
            uint32_t mbox_reg_state32[CXL_MAILBOX_REGISTERS_LENGTH / 4];
            uint64_t mbox_reg_state64[CXL_MAILBOX_REGISTERS_LENGTH / 8];
        };
        struct cel_log {
            uint16_t opcode;
            uint16_t effect;
        } cel_log[1 << 16];
        size_t cel_size;
    };

    struct {
        bool set;
        uint64_t last_set;
        uint64_t host_set;
    } timestamp;

    /* memory region for persistent memory, HDM */
    MemoryRegion *pmem;

    /* memory region for volatile  memory, HDM */
    MemoryRegion *vmem;
} CXLDeviceState;

/* Initialize the register block for a device */
void cxl_device_register_block_init(Object *obj, CXLDeviceState *dev);

/* Set up default values for the register block */
void cxl_device_register_init_common(CXLDeviceState *dev);

/* CXL 2.0 - 8.2.8.1 */
REG32(CXL_DEV_CAP_ARRAY, 0) /* 48b!?!?! */
    FIELD(CXL_DEV_CAP_ARRAY, CAP_ID, 0, 16)
    FIELD(CXL_DEV_CAP_ARRAY, CAP_VERSION, 16, 8)
REG32(CXL_DEV_CAP_ARRAY2, 4) /* We're going to pretend it's 64b */
    FIELD(CXL_DEV_CAP_ARRAY2, CAP_COUNT, 0, 16)

/*
 * Helper macro to initialize capability headers for CXL devices.
 *
 * In the 8.2.8.2, this is listed as a 128b register, but in 8.2.8, it says:
 * > No registers defined in Section 8.2.8 are larger than 64-bits wide so that
 * > is the maximum access size allowed for these registers. If this rule is not
 * > followed, the behavior is undefined
 *
 * Here we've chosen to make it 4 dwords. The spec allows any pow2 multiple
 * access to be used for a register (2 qwords, 8 words, 128 bytes).
 */
#define CXL_DEVICE_CAPABILITY_HEADER_REGISTER(n, offset)                            \
    REG32(CXL_DEV_##n##_CAP_HDR0, offset)                 \
        FIELD(CXL_DEV_##n##_CAP_HDR0, CAP_ID, 0, 16)      \
        FIELD(CXL_DEV_##n##_CAP_HDR0, CAP_VERSION, 16, 8) \
    REG32(CXL_DEV_##n##_CAP_HDR1, offset + 4)             \
        FIELD(CXL_DEV_##n##_CAP_HDR1, CAP_OFFSET, 0, 32)  \
    REG32(CXL_DEV_##n##_CAP_HDR2, offset + 8)             \
        FIELD(CXL_DEV_##n##_CAP_HDR2, CAP_LENGTH, 0, 32)

CXL_DEVICE_CAPABILITY_HEADER_REGISTER(DEVICE, CXL_DEVICE_CAP_HDR1_OFFSET)
CXL_DEVICE_CAPABILITY_HEADER_REGISTER(MAILBOX, CXL_DEVICE_CAP_HDR1_OFFSET + \
                                               CXL_DEVICE_CAP_REG_SIZE)
CXL_DEVICE_CAPABILITY_HEADER_REGISTER(MEMORY_DEVICE,
                                      CXL_DEVICE_CAP_HDR1_OFFSET +
                                          CXL_DEVICE_CAP_REG_SIZE * 2)

int cxl_initialize_mailbox(CXLDeviceState *cxl_dstate);
void cxl_process_mailbox(CXLDeviceState *cxl_dstate);

#define cxl_device_cap_init(dstate, reg, cap_id)                                   \
    do {                                                                           \
        uint32_t *cap_hdrs = dstate->caps_reg_state32;                             \
        int which = R_CXL_DEV_##reg##_CAP_HDR0;                                    \
        cap_hdrs[which] =                                                          \
            FIELD_DP32(cap_hdrs[which], CXL_DEV_##reg##_CAP_HDR0, CAP_ID, cap_id); \
        cap_hdrs[which] = FIELD_DP32(                                              \
            cap_hdrs[which], CXL_DEV_##reg##_CAP_HDR0, CAP_VERSION, 1);            \
        cap_hdrs[which + 1] =                                                      \
            FIELD_DP32(cap_hdrs[which + 1], CXL_DEV_##reg##_CAP_HDR1,              \
                       CAP_OFFSET, CXL_##reg##_REGISTERS_OFFSET);                  \
        cap_hdrs[which + 2] =                                                      \
            FIELD_DP32(cap_hdrs[which + 2], CXL_DEV_##reg##_CAP_HDR2,              \
                       CAP_LENGTH, CXL_##reg##_REGISTERS_LENGTH);                  \
    } while (0)

REG32(CXL_DEV_MAILBOX_CAP, 0)
    FIELD(CXL_DEV_MAILBOX_CAP, PAYLOAD_SIZE, 0, 5)
    FIELD(CXL_DEV_MAILBOX_CAP, INT_CAP, 5, 1)
    FIELD(CXL_DEV_MAILBOX_CAP, BG_INT_CAP, 6, 1)
    FIELD(CXL_DEV_MAILBOX_CAP, MSI_N, 7, 4)

REG32(CXL_DEV_MAILBOX_CTRL, 4)
    FIELD(CXL_DEV_MAILBOX_CTRL, DOORBELL, 0, 1)
    FIELD(CXL_DEV_MAILBOX_CTRL, INT_EN, 1, 1)
    FIELD(CXL_DEV_MAILBOX_CTRL, BG_INT_EN, 2, 1)

/* XXX: actually a 64b register */
REG32(CXL_DEV_MAILBOX_CMD, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, COMMAND, 0, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, COMMAND_SET, 8, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, LENGTH, 16, 20)

/* XXX: actually a 64b register */
REG32(CXL_DEV_MAILBOX_STS, 0x10)
    FIELD(CXL_DEV_MAILBOX_STS, BG_OP, 0, 1)
    FIELD(CXL_DEV_MAILBOX_STS, ERRNO, 32, 16)
    FIELD(CXL_DEV_MAILBOX_STS, VENDOR_ERRNO, 48, 16)

/* XXX: actually a 64b register */
REG32(CXL_DEV_BG_CMD_STS, 0x18)
    FIELD(CXL_DEV_BG_CMD_STS, BG, 0, 16)
    FIELD(CXL_DEV_BG_CMD_STS, DONE, 16, 7)
    FIELD(CXL_DEV_BG_CMD_STS, ERRNO, 32, 16)
    FIELD(CXL_DEV_BG_CMD_STS, VENDOR_ERRNO, 48, 16)

REG32(CXL_DEV_CMD_PAYLOAD, 0x20)

/* XXX: actually a 64b registers */
REG32(CXL_MEM_DEV_STS, 0)
    FIELD(CXL_MEM_DEV_STS, FATAL, 0, 1)
    FIELD(CXL_MEM_DEV_STS, FW_HALT, 1, 1)
    FIELD(CXL_MEM_DEV_STS, MEDIA_STATUS, 2, 2)
    FIELD(CXL_MEM_DEV_STS, MBOX_READY, 4, 1)
    FIELD(CXL_MEM_DEV_STS, RESET_NEEDED, 5, 3)

typedef struct cxl_type3_dev {
    /* Private */
    PCIDevice parent_obj;

    /* Properties */
    uint64_t size;
    HostMemoryBackend *hostmem;
    HostMemoryBackend *lsa;

    /* State */
    CXLComponentState cxl_cstate;
    CXLDeviceState cxl_dstate;
} CXLType3Dev;

#ifndef TYPE_CXL_TYPE3_DEV
#define TYPE_CXL_TYPE3_DEV "cxl-type3"
#endif
#define CT3(obj) OBJECT_CHECK(CXLType3Dev, (obj), TYPE_CXL_TYPE3_DEV)
OBJECT_DECLARE_TYPE(CXLType3Device, CXLType3Class, CXL_TYPE3_DEV)

struct CXLType3Class {
    /* Private */
    PCIDeviceClass parent_class;

    /* public */
    uint64_t (*get_lsa_size)(CXLType3Dev *ct3d);

    uint64_t (*get_lsa)(CXLType3Dev *ct3d, void *buf, uint64_t size,
                        uint64_t offset);
    void (*set_lsa)(CXLType3Dev *ct3d, const void *buf, uint64_t size,
                    uint64_t offset);
};

#endif
