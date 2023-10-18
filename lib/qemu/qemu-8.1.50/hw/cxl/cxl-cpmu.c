/*
 * CXL Performance Monitoring Unit
 *
 * Copyright(C) 2022 Jonathan Cameron - Huawei
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/cxl/cxl.h"

#include "hw/pci/msi.h"
#include "hw/pci/msix.h"

static uint64_t cpmu_read(void *opaque, hwaddr offset, unsigned size)
{
    CPMUState *cpmu = opaque;
    uint64_t retval = 0;
    int index;

    /* I'm lazy - lets assume 4 or 8 byte reads only - fix that up later. */
    switch (offset) {
    case A_CXL_CPMU_CAP:
        retval = FIELD_DP64(retval, CXL_CPMU_CAP,
                            NUM_COUNTERS, CPMU_NUM_COUNTERS - 1);
        retval = FIELD_DP64(retval, CXL_CPMU_CAP, COUNTER_WIDTH, 48);
        retval = FIELD_DP64(retval, CXL_CPMU_CAP,
                            NUM_EVENT_GROUPS, CPMU_NUM_EVENT_GROUPS - 1);
        retval = FIELD_DP64(retval, CXL_CPMU_CAP, FILTERS_SUP, 1);
        retval = FIELD_DP64(retval, CXL_CPMU_CAP, MSI_N, cpmu->msi_n);
        retval = FIELD_DP64(retval, CXL_CPMU_CAP, WRITEABLE_WHEN_EN, 1);
        retval = FIELD_DP64(retval, CXL_CPMU_CAP, FREEZE, 1);
        retval = FIELD_DP64(retval, CXL_CPMU_CAP, INT, 1);
        break;
    case A_CXL_CPMU_OVERFLOW_STS:
        retval = cpmu->overflow_status_bm;
        break;
    case A_CXL_CPMU_FREEZE:
        retval = cpmu->freeze_status_bm;
        break;
    case A_CXL_CPMU_EVENT_CAP0:
        /* Event group 0, clock ticks */
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP0, SUPPORTED_EVENTS, 1);
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP0, EVENT_GROUP_ID, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP0,
                            EVENT_VENDOR_ID, 0x1e98);
        break;
    case A_CXL_CPMU_EVENT_CAP1:
        /* Random mashup */
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP1, SUPPORTED_EVENTS,
                            0xFF);
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP1, EVENT_GROUP_ID, 0x10);
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP1, EVENT_VENDOR_ID,
                            0x1e98);
        break;
    case A_CXL_CPMU_EVENT_CAP2:
        /* Random mashup */
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP2, SUPPORTED_EVENTS,
                            0xFF);
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP2, EVENT_GROUP_ID, 0x12);
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP2, EVENT_VENDOR_ID,
                            0x1e98);
        break;
    case A_CXL_CPMU_EVENT_CAP3:
        /* Random mashup - vendor specific */
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP3, SUPPORTED_EVENTS,
                            0xFF);
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP3, EVENT_GROUP_ID, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_EVENT_CAP3, EVENT_VENDOR_ID,
                            0x19e5);
        break;

    case A_CXL_CPMU_CNT0_CFG:
        /* Lets' make this a fixed function counter doing only the first set */
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, TYPE,
                            CXL_CPMU_CNT_CFG_TYPE_FIXED_FUN);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, ENABLE,
                            cpmu->counter_enable[0]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, INT_ON_OVERFLOW,
                            cpmu->int_en[0]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, FREEZE_ON_OVERFLOW,
                            cpmu->freeze_en[0]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, EDGE, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, INVERT, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, THRESHOLD, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, EVENTS, 1);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT0_CFG, EVENT_GROUP_ID_IDX, 0);
        break;
    case A_CXL_CPMU_CNT1_CFG:
    case A_CXL_CPMU_CNT2_CFG:
        /* A couple of configurable counters */
        index = (offset - A_CXL_CPMU_CNT0_CFG) / sizeof(uint64_t);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG, TYPE,
                            CXL_CPMU_CNT_CFG_TYPE_CONFIGURABLE);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG, ENABLE,
                            cpmu->counter_enable[index]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG, INT_ON_OVERFLOW,
                            cpmu->int_en[index]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG, FREEZE_ON_OVERFLOW,
                            cpmu->freeze_en[index]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG, EDGE, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG, INVERT, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG, THRESHOLD, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG, EVENTS, 0xFF);
        /* Fixme - perhaps make this flexible at somepoint */
        retval = FIELD_DP64(retval, CXL_CPMU_CNT1_CFG,
                            EVENT_GROUP_ID_IDX, 1);
        break;
    case A_CXL_CPMU_CNT3_CFG:
        /* Try to break my code! */
        index = (offset - A_CXL_CPMU_CNT0_CFG) / sizeof(uint64_t);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG,
                            TYPE, CXL_CPMU_CNT_CFG_TYPE_FIXED_FUN);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG,
                            ENABLE, cpmu->counter_enable[index]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG,
                            INT_ON_OVERFLOW, cpmu->int_en[index]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG,
                            FREEZE_ON_OVERFLOW, cpmu->freeze_en[index]);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG, EDGE, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG, INVERT, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG, THRESHOLD, 0);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG, EVENTS, 1 << 3);
        retval = FIELD_DP64(retval, CXL_CPMU_CNT3_CFG,
                            EVENT_GROUP_ID_IDX, 1);
        break;
    case A_CXL_CPMU_FILTER0_CNT0_CFG:
        retval = FIELD_DP64(retval, CXL_CPMU_FILTER0_CNT0_CFG, VALUE, 0xffff);
        break;
    case A_CXL_CPMU_FILTER0_CNT1_CFG:
        index = 1;
        retval = FIELD_DP64(retval, CXL_CPMU_FILTER0_CNT1_CFG, VALUE,
                            cpmu->filter_value[0][index]);
        break;
    case A_CXL_CPMU_FILTER0_CNT2_CFG:
        index = 2;
        retval = FIELD_DP64(retval, CXL_CPMU_FILTER0_CNT2_CFG, VALUE,
                            cpmu->filter_value[0][index]);
        break;
    case A_CXL_CPMU_FILTER0_CNT3_CFG:
        index = 3;
        retval = FIELD_DP64(retval, CXL_CPMU_FILTER0_CNT3_CFG, VALUE,
                            cpmu->filter_value[0][index]);
        break;
    case A_CXL_CPMU_CNT0:
    case A_CXL_CPMU_CNT1:
    case A_CXL_CPMU_CNT2:
    case A_CXL_CPMU_CNT3:
        index = (offset - A_CXL_CPMU_CNT0) / sizeof(uint64_t);
        retval = cpmu->counter[index] & 0xffffffffffff;

        break;
    default:
        break;
    }

    return retval;
}

static void cpmu_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    CPMUState *cpmu = opaque;
    int index;

    /* Lazy and assume correct size reads and writes for now  */
    switch (offset) {
    case 0:
        break;
    case 8:
        break;
    case A_CXL_CPMU_FREEZE:
        cpmu->freeze_status_bm = value;
        break;
    case A_CXL_CPMU_OVERFLOW_STS:
        cpmu->overflow_status_bm &= ~value;
        break;
    case A_CXL_CPMU_CNT0_CFG:
    case A_CXL_CPMU_CNT1_CFG:
    case A_CXL_CPMU_CNT2_CFG:
    case A_CXL_CPMU_CNT3_CFG:
        index = (offset - A_CXL_CPMU_CNT0_CFG) / sizeof(uint64_t);
        cpmu->int_en[index] =
            FIELD_EX32(value, CXL_CPMU_CNT0_CFG, INT_ON_OVERFLOW);
        cpmu->freeze_en[index] =
            FIELD_EX32(value, CXL_CPMU_CNT0_CFG, FREEZE_ON_OVERFLOW);
        cpmu->counter_enable[index] =
            FIELD_EX32(value, CXL_CPMU_CNT0_CFG, ENABLE);
        break;
    case A_CXL_CPMU_FILTER0_CNT1_CFG:
        index = 1;
        cpmu->filter_value[0][index] =
            FIELD_EX32(value, CXL_CPMU_FILTER0_CNT1_CFG, VALUE);
        break;
    case A_CXL_CPMU_FILTER0_CNT2_CFG:
        index = 2;
        break;
    case A_CXL_CPMU_FILTER0_CNT3_CFG:
        index = 3;
        cpmu->filter_value[0][index] =
            FIELD_EX32(value, CXL_CPMU_FILTER0_CNT1_CFG, VALUE);
        break;
    case A_CXL_CPMU_CNT0:
    case A_CXL_CPMU_CNT1:
    case A_CXL_CPMU_CNT2:
    case A_CXL_CPMU_CNT3:
        index = (offset - A_CXL_CPMU_CNT0) / sizeof(uint64_t);
        cpmu->counter[index] = value & 0xffffffffffff;
        break;
    }

    return;
}

static const MemoryRegionOps cpmu_ops = {
    .read = cpmu_read,
    .write = cpmu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};


static void cpmu_counter_update(void *opaque)
{
    CPMUState *cpmu = opaque;
    PCIDevice *pdev = PCI_DEVICE(cpmu->private);
    bool interrupt_needed = false;
    int i;

    timer_del(cpmu->timer);

    for (i = 0; i < CPMU_NUM_COUNTERS; i++) {
        /*
         * TODO: check enabled, not frozen etc
         *
         * Hack to make the numbers get bigger!  Ideally for at least
         * some types of event we could hook it up to actual accesses.
         */
        uint64_t previous = cpmu->counter[i];
        if (cpmu->counter_enable[i]) {
            switch (i) {
            case 0:
                cpmu->counter[i] += (1ULL << 44) * 1 + 7;
                break;
            case 1:
                cpmu->counter[i] += (1ULL << 43) * 1 + 3;
                break;
            case 2:
                cpmu->counter[i] += (1ULL << 43) * 1 + 7;
                break;
            default:
                cpmu->counter[i] += 30;
                break;
            }
            if (cpmu->counter[i] / (1ULL << 48) != previous / (1ULL << 48)) {
                cpmu->overflow_status_bm |= (1 << i);
                if (cpmu->int_en[i]) {
                    interrupt_needed = true;
                }
            }
        }
    }
    timer_mod(cpmu->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000000);
    if (interrupt_needed) {

        if (msix_enabled(pdev)) {
            msix_notify(pdev, cpmu->msi_n);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, cpmu->msi_n);
        }
    }
}

void cxl_cpmu_register_block_init(Object *obj, CXLDeviceState *cxl_dstate,
                                  int id, uint8_t msi_n)
{
    CPMUState *cpmu = &cxl_dstate->cpmu[id];
    MemoryRegion *registers = &cxl_dstate->cpmu_registers[id];
    g_autofree gchar *name = g_strdup_printf("cpmu%d-registers", id);

    cpmu->msi_n = msi_n;
    cpmu->private = obj;
    memory_region_init_io(registers, obj, &cpmu_ops, cpmu,
                          name, pow2ceil(CXL_CPMU_SIZE));
    cpmu->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cpmu_counter_update,
                               &cxl_dstate->cpmu[id]);
    timer_mod(cpmu->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000000);

    /* Need to force 64k Alignment in the bar */
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_CPMU_OFFSET(id), registers);
}

void cxl_cpmu_register_block_init2(Object *obj, CPMUState *cpmu,
                                   MemoryRegion *registers,
                                   int id, uint8_t msi_n)
{
    g_autofree gchar *name = g_strdup_printf("cpmu%d-registers", id);

    cpmu->msi_n = msi_n;
    cpmu->private = obj;
    memory_region_init_io(registers, obj, &cpmu_ops, cpmu,  name,
                          pow2ceil(CXL_CPMU_SIZE));
    cpmu->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cpmu_counter_update, cpmu);
    timer_mod(cpmu->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000000);
}
