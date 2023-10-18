/*
 * QEMU CXL Performance Monitoring Unit
 *
 * Copyright (c) 2021 Jonathan Cameron - Huawei
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "hw/register.h"

#ifndef _CXL_CPMU_H_
#define _CXL_CPMU_H_

REG32(CXL_CPMU_CAP, 0)
    FIELD(CXL_CPMU_CAP, NUM_COUNTERS, 0, 6)
    FIELD(CXL_CPMU_CAP, COUNTER_WIDTH, 8, 8)
    FIELD(CXL_CPMU_CAP, NUM_EVENT_GROUPS, 20, 5)
    FIELD(CXL_CPMU_CAP, FILTERS_SUP, 32, 8)
    FIELD(CXL_CPMU_CAP, MSI_N, 44, 4)
    FIELD(CXL_CPMU_CAP, WRITEABLE_WHEN_EN, 48, 1)
    FIELD(CXL_CPMU_CAP, FREEZE, 49, 1)
    FIELD(CXL_CPMU_CAP, INT, 50, 1)
/* Register at offset 0x8 is reserved */
REG32(CXL_CPMU_OVERFLOW_STS, 0x10)
REG32(CXL_CPMU_FREEZE, 0x18)
/* Registers at offset 20 + reserved */

#define CXL_CPMU_EVENT_CAP(n) \
    REG64(CXL_CPMU_EVENT_CAP##n, 0x100 + 8 * (n))               \
        FIELD(CXL_CPMU_EVENT_CAP##n, SUPPORTED_EVENTS, 0, 32)   \
        FIELD(CXL_CPMU_EVENT_CAP##n, EVENT_GROUP_ID, 32, 16)    \
        FIELD(CXL_CPMU_EVENT_CAP##n, EVENT_VENDOR_ID, 48, 16)

CXL_CPMU_EVENT_CAP(0)
CXL_CPMU_EVENT_CAP(1)
CXL_CPMU_EVENT_CAP(2)
CXL_CPMU_EVENT_CAP(3)

#define CXL_CPMU_CNT_CFG(n) \
    REG64(CXL_CPMU_CNT##n##_CFG, 0x200 + 8 * (n))                     \
        FIELD(CXL_CPMU_CNT##n##_CFG, TYPE, 0, 2)                      \
        FIELD(CXL_CPMU_CNT##n##_CFG, ENABLE, 8, 1)                    \
        FIELD(CXL_CPMU_CNT##n##_CFG, INT_ON_OVERFLOW, 9, 1)           \
        FIELD(CXL_CPMU_CNT##n##_CFG, FREEZE_ON_OVERFLOW, 10, 1)       \
        FIELD(CXL_CPMU_CNT##n##_CFG, EDGE, 11, 1)                     \
        FIELD(CXL_CPMU_CNT##n##_CFG, INVERT, 12, 1)                   \
        FIELD(CXL_CPMU_CNT##n##_CFG, THRESHOLD, 16, 8)                \
        FIELD(CXL_CPMU_CNT##n##_CFG, EVENTS, 24, 32)                  \
        FIELD(CXL_CPMU_CNT##n##_CFG, EVENT_GROUP_ID_IDX, 59, 5)

#define CXL_CPMU_CNT_CFG_TYPE_FREE_RUN 0
#define CXL_CPMU_CNT_CFG_TYPE_FIXED_FUN 1
#define CXL_CPMU_CNT_CFG_TYPE_CONFIGURABLE 2
CXL_CPMU_CNT_CFG(0)
CXL_CPMU_CNT_CFG(1)
CXL_CPMU_CNT_CFG(2)
CXL_CPMU_CNT_CFG(3)

#define CXL_CPMU_FILTER_CFG(n, f)                                       \
    REG64(CXL_CPMU_FILTER##f##_CNT##n##_CFG, 0x400 + 4 * ((f) + (n) * 8)) \
        FIELD(CXL_CPMU_FILTER##f##_CNT##n##_CFG, VALUE, 0, 16)

/* Only HDM decoder filter suppored - no effect on first counter */
CXL_CPMU_FILTER_CFG(0, 0)
CXL_CPMU_FILTER_CFG(1, 0)
CXL_CPMU_FILTER_CFG(2, 0)
CXL_CPMU_FILTER_CFG(3, 0)

#define CXL_CPMU_CNT(n)                     \
    REG64(CXL_CPMU_CNT##n, 0xc00 + 8 * (n))

CXL_CPMU_CNT(0)
CXL_CPMU_CNT(1)
CXL_CPMU_CNT(2)
CXL_CPMU_CNT(3)

typedef struct CPMUState {
#define CPMU_NUM_COUNTERS 4
#define CPMU_NUM_EVENT_GROUPS 4
#define CPMU_NUM_FILTERS 1
    bool counter_enable[CPMU_NUM_COUNTERS];
    bool int_en[CPMU_NUM_COUNTERS];
    bool freeze_en[CPMU_NUM_COUNTERS];
    bool filter_value[CPMU_NUM_FILTERS][CPMU_NUM_COUNTERS];
    uint64_t counter[CPMU_NUM_COUNTERS];
    uint64_t overflow_status_bm;
    uint64_t freeze_status_bm;
    uint8_t msi_n;
    QEMUTimer *timer;
    void *private;
} CPMUState;

typedef struct cxl_device_state CXLDeviceState;
void cxl_cpmu_register_block_init(Object *obj,
                                  CXLDeviceState *cxl_dstate,
                                  int id, uint8_t msi_n);

void cxl_cpmu_register_block_init2(Object *obj, CPMUState *cpmu,
                                   MemoryRegion *registers,
                                   int id, uint8_t msi_n);
#endif
