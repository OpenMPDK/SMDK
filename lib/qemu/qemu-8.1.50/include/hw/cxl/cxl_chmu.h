/*
 * QEMU CXL Hotness Monitoring Unit
 */

#include "hw/register.h"

#ifndef _CXL_CHMU_H_
#define _CXL_CHMU_H_

#define CXL_HMU_MAX_CHMU_CNT    8

/* Assumption */
#define CXL_HMU_HOTLIST_SIZE    64
#define CXL_HMU_BITMAP_CNT      2 /* 16GB (Device Capacity) / 8GB = 2 */

#define CXL_HMU_HOTLIST_LEN     8  /* bytes */
#define CXL_HMU_BITMAP_LEN      8  /* bytes */

#define CXL_HMU_INSTANCE_LEN	(0x70 + \
		CXL_HMU_BITMAP_CNT * CXL_HMU_BITMAP_LEN + \
		CXL_HMU_HOTLIST_SIZE * CXL_HMU_HOTLIST_LEN)

/* CXL r3.1 ECN - CXL Hotness Monitoring Unit v0.93 */
REG64(CXL_CHMU_COMMON_CAP1, 0x0)
    FIELD(CXL_CHMU_COMMON_CAP1, VERSION, 0, 4)
    FIELD(CXL_CHMU_COMMON_CAP1, NUM_CHMUS, 8, 8)
REG64(CXL_CHMU_COMMON_CAP2, 0x8)
    FIELD(CXL_CHMU_COMMON_CAP2, INSTANCE_LEN, 0, 16)

/* CHMU Capability 512 bits (63:0) */
#define CXL_CHMU_CAP1(n) \
    REG64(CXL_CHMU_CAP1_##n, 0x10 + CXL_HMU_INSTANCE_LEN * (n))    \
        FIELD(CXL_CHMU_CAP1_##n, MSI_N, 0, 4)                           \
        FIELD(CXL_CHMU_CAP1_##n, HOTLIST_OVERFLOW_INT_SUP, 4, 1)        \
        FIELD(CXL_CHMU_CAP1_##n, HOTLIST_LEVELS_CROSSING_INT_SUP, 5, 1) \
        FIELD(CXL_CHMU_CAP1_##n, EPOCH_TYPE, 6, 2)                      \
        FIELD(CXL_CHMU_CAP1_##n, TRACKED_M2S_REQ, 8, 8)                 \
        FIELD(CXL_CHMU_CAP1_##n, MAX_EPOCH_LEN, 16, 16)                 \
        FIELD(CXL_CHMU_CAP1_##n, MIN_EPOCH_LEN, 32, 16)                 \
        FIELD(CXL_CHMU_CAP1_##n, HOTLIST_SIZE, 48, 16)

#define EPOCH_TYPE_GLOBAL       0
#define EPOCH_TYPE_PER_COUNTER  1

/* CHMU Capability 512 bits (127:64) */
#define CXL_CHMU_CAP2(n) \
    REG64(CXL_CHMU_CAP2_##n, 0x18 + CXL_HMU_INSTANCE_LEN * (n))    \
        FIELD(CXL_CHMU_CAP2_##n, SUPPORTED_UNIT_SIZE, 0, 32)            \
        FIELD(CXL_CHMU_CAP2_##n, SUPPORTED_DOWNSAMPLING_FACTOR, 32, 16) \
        FIELD(CXL_CHMU_CAP2_##n, CAP_FLAGS, 48, 16)

/* CHMU Capability 512 bits (191:128) */
#define CXL_CHMU_CAP3(n) \
    REG64(CXL_CHMU_CAP3_##n, 0x20 + CXL_HMU_INSTANCE_LEN * (n))    \
        FIELD(CXL_CHMU_CAP3_##n, RANGE_CFG_BITMAP_REG_OFFSET, 0, 64)

/* CHMU Capability 512 bits (255:192) */
#define CXL_CHMU_CAP4(n) \
    REG64(CXL_CHMU_CAP4_##n, 0x28 + CXL_HMU_INSTANCE_LEN * (n))    \
        FIELD(CXL_CHMU_CAP4_##n, HOTLIST_REG_OFFSET, 0, 64)

/* Let's assume that there's only one CHMU per the CXL device */
CXL_CHMU_CAP1(0)
CXL_CHMU_CAP2(0)
CXL_CHMU_CAP3(0)
CXL_CHMU_CAP4(0)

/* CHMU Configuration 256 bits (63:0) */
#define CXL_CHMU_CFG1(n) \
    REG64(CXL_CHMU_CFG1_##n, 0x50 + CXL_HMU_INSTANCE_LEN * (n))    \
        FIELD(CXL_CHMU_CFG1_##n, M2S_REQ_TO_TRACK, 0, 8)                \
        FIELD(CXL_CHMU_CFG1_##n, FLAGS, 8, 8)                           \
        FIELD(CXL_CHMU_CFG1_##n, CONTROL, 16, 16)                       \
        FIELD(CXL_CHMU_CFG1_##n, HOTNESS_THRESHOLD, 32, 32)             \

#define CXL_CHMU_CFG_CONTROL_ENABLE (1 << 0)
#define CXL_CHMU_CFG_CONTROL_RESET  (1 << 1)

/* CHMU Configuration 256 bits (127:64) */
#define CXL_CHMU_CFG2(n) \
    REG64(CXL_CHMU_CFG2_##n, 0x58 + CXL_HMU_INSTANCE_LEN * (n))    \
        FIELD(CXL_CHMU_CFG2_##n, UNIT_SIZE, 0, 32)                      \
        FIELD(CXL_CHMU_CFG2_##n, DOWN_SAMPLING_FACTOR, 32, 8)           \
        FIELD(CXL_CHMU_CFG2_##n, REPORTING_MODE, 40, 8)                 \
        FIELD(CXL_CHMU_CFG2_##n, EPOCH_LEN, 48, 16)                     \

#define CXL_CHMU_REPORTING_MODE_EPOCH       0x0
#define CXL_CHMU_REPORTING_MODE_ALWAYS_ON   0x1

/* CHMU Configuration 256 bits (191:128) */
#define CXL_CHMU_CFG3(n) \
    REG64(CXL_CHMU_CFG3_##n, 0x60 + CXL_HMU_INSTANCE_LEN * (n))    \
        FIELD(CXL_CHMU_CFG3_##n, HOTLIST_NOTI_THRESHOLD, 0, 16)

/* Let's assume that there's only one CHMU per the CXL device */
CXL_CHMU_CFG1(0)
CXL_CHMU_CFG2(0)
CXL_CHMU_CFG3(0)

/* CHMU Status 64 bits */
#define CXL_CHMU_STAT(n) \
    REG64(CXL_CHMU_STAT##n, 0x70 + CXL_HMU_INSTANCE_LEN * (n))     \
        FIELD(CXL_CHMU_STAT##n, STATUS, 0, 16)                          \
        FIELD(CXL_CHMU_STAT##n, OP_IN_PROGRESS, 16, 16)                 \
        FIELD(CXL_CHMU_STAT##n, COUNTER_WIDTH, 32, 8)                   \
        FIELD(CXL_CHMU_STAT##n, OVERFLOW_INT_STATUS, 40, 8)

#define CXL_CHMU_STAT_OVERFLOW_INT_OVERFLOW (1 << 0)
#define CXL_CHMU_STAT_OVERFLOW_INT_LEVEL_CROSS (1 << 1)

/* Let's assume that there's only one CHMU per the CXL device */
CXL_CHMU_STAT(0)

#define CXL_CHMU_HOTLIST_HEAD_TAIL(n) \
    REG32(CXL_CHMU_HOTLIST_HEAD_TAIL##n,                                \
          0x78 + CXL_HMU_INSTANCE_LEN * (n))                       \
        FIELD(CXL_CHMU_HOTLIST_HEAD_TAIL##n, HOTLIST_HEAD, 0, 16)       \
        FIELD(CXL_CHMU_HOTLIST_HEAD_TAIL##n, HOTLIST_TAIL, 16, 16)

/* Let's assume that there's only one CHMU per the CXL device */
CXL_CHMU_HOTLIST_HEAD_TAIL(0)

#define CXL_CHMU_RANGE_CFG_BITMAP(n, i) \
    REG64(CXL_CHMU_RANGE_CFG_BITMAP##i##_##n,                           \
          0x80 + CXL_HMU_BITMAP_LEN * (i) +                             \
          CXL_HMU_INSTANCE_LEN * (n))                              \
        FIELD(CXL_CHMU_RANGE_CFG_BITMAP##i##_##n, BITMAP, 0, 64)

/* Let's assume that it's 16GB device. 16GB / 8GB = 2 */
CXL_CHMU_RANGE_CFG_BITMAP(0, 0)
CXL_CHMU_RANGE_CFG_BITMAP(0, 1)

#define CXL_CHMU_HOTLIST(n) \
    REG64(CXL_CHMU_HOTLIST##n,                                          \
          0x80 + CXL_HMU_BITMAP_LEN * CXL_HMU_BITMAP_CNT +              \
          CXL_HMU_INSTANCE_LEN * (n))                              \
        FIELD(CXL_CHMU_HOTLIST##n, HOTLIST_ENTRY, 0, 64)

/* Let's assume that there's only one CHMU per the CXL device */
CXL_CHMU_HOTLIST(0)

/* FIXME */
typedef struct CHMUState {
    QEMUTimer *timer;
    void *private;

    /* CHMU Configurations */
    struct {
        uint8_t m2s_req_to_track;
        uint8_t flags;
        uint16_t control;
        uint32_t hotness_threshold;
        uint32_t unit_size;
        uint8_t down_sampling_factor;
        uint8_t reporting_mode;
        uint16_t epoch_len;
        uint16_t hotlist_noti_threshold;
    } cfg[CXL_HMU_MAX_CHMU_CNT];

    /* CHMU Status */
    struct {
        uint16_t status;
        uint16_t op_in_progress;
        uint8_t counter_width;
        uint8_t overflow_int_status;
    } stat[CXL_HMU_MAX_CHMU_CNT];

    uint8_t msi_n[CXL_HMU_MAX_CHMU_CNT];
    uint64_t hotlist[CXL_HMU_MAX_CHMU_CNT][CXL_HMU_HOTLIST_SIZE];
    uint16_t hotlist_head_idx[CXL_HMU_MAX_CHMU_CNT];
    uint16_t hotlist_tail_idx[CXL_HMU_MAX_CHMU_CNT];
    uint64_t range_bitmap[CXL_HMU_MAX_CHMU_CNT][CXL_HMU_BITMAP_CNT];
} CHMUState;

void cxl_chmu_register_block_init(Object *obj,
                                  CXLDeviceState *cxl_dstate,
                                  int id, uint8_t msi_n);
#endif
