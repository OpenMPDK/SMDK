/*
 * CXL Hotness Monitoring Unit
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/guest-random.h"
#include "hw/cxl/cxl.h"

#include "hw/pci/msi.h"
#include "hw/pci/msix.h"

static uint32_t is_tracked(CHMUState *chmu, uint64_t unit_id, int width)
{
    uint64_t addr;
    uint64_t bitmap=0;
    int reg_idx;
    int bit_idx;
    int result;
    addr = unit_id << width;
    reg_idx = addr / (1ULL << 33); /* 8GB per bitmap */

    switch(reg_idx) {
        case 0:
            bitmap = FIELD_DP64(bitmap, CXL_CHMU_RANGE_CFG_BITMAP0_0, BITMAP, chmu->range_bitmap[0][reg_idx]);
            break;
        case 1:
            bitmap = FIELD_DP64(bitmap, CXL_CHMU_RANGE_CFG_BITMAP1_0, BITMAP, chmu->range_bitmap[0][reg_idx]);
            break;
        default:
            break;
    }

    bit_idx = addr % (1ULL << 33) / (1ULL << 28); /* 256GB per bit in bitmap */

    result = bitmap >> bit_idx;
    result = result % 2;

    if (result)
        fprintf(stderr, "unit_id=0x%lx, reg_idx=%d, bit_idx=%d, "
                "is_tracked()=%d, TRUE\n", unit_id, reg_idx, bit_idx, result);
    else
        fprintf(stderr, "unit_id=0x%lx, reg_idx=%d, bit_idx=%d, "
                "is_tracked()=%d, FALSE\n", unit_id, reg_idx, bit_idx, result);

    return result;
}

static uint32_t chmu_get_random(int bit)
{
    uint32_t num;
    qemu_guest_getrandom_nofail(&num, sizeof(num));
    return (num & ((1 << bit) - 1));
}

static uint64_t chmu_read(void *opaque, hwaddr offset, unsigned size)
{
    CHMUState *chmu = opaque;
    uint64_t retval = 0;

    /* I'm lazy - lets assume 4 or 8 byte reads only - fix that up later. */
    switch (offset) {
    case A_CXL_CHMU_COMMON_CAP1:
        retval = FIELD_DP64(retval, CXL_CHMU_COMMON_CAP1, VERSION, 1);
        retval = FIELD_DP64(retval, CXL_CHMU_COMMON_CAP1, NUM_CHMUS, 1);
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_COMMON_CAP2:
        retval = FIELD_DP64(retval, CXL_CHMU_COMMON_CAP2, INSTANCE_LEN, CXL_HMU_INSTANCE_LEN); /* 2 Bitmaps, 64 Hotlists */
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_CAP1_0:
        retval = FIELD_DP64(retval, CXL_CHMU_CAP1_0, MSI_N, chmu->msi_n[0]);
        retval = FIELD_DP64(retval, CXL_CHMU_CAP1_0,
                            HOTLIST_OVERFLOW_INT_SUP, 1);
        retval = FIELD_DP64(retval, CXL_CHMU_CAP1_0,
                            HOTLIST_LEVELS_CROSSING_INT_SUP, 1);
        retval = FIELD_DP64(retval, CXL_CHMU_CAP1_0, EPOCH_TYPE,
                            EPOCH_TYPE_GLOBAL);
        retval = FIELD_DP64(retval, CXL_CHMU_CAP1_0, TRACKED_M2S_REQ, 0x3F); /* 0b111111 */
        retval = FIELD_DP64(retval, CXL_CHMU_CAP1_0, MAX_EPOCH_LEN, 0); /* Invalid */
        retval = FIELD_DP64(retval, CXL_CHMU_CAP1_0, MIN_EPOCH_LEN, 0); /* Invalid */
        retval = FIELD_DP64(retval, CXL_CHMU_CAP1_0, HOTLIST_SIZE,
                            CXL_HMU_HOTLIST_SIZE);

        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_CAP2_0:
        retval = FIELD_DP64(retval, CXL_CHMU_CAP2_0, SUPPORTED_UNIT_SIZE, 0x100000); /* 256MB only */
        retval = FIELD_DP64(retval, CXL_CHMU_CAP2_0,
                            SUPPORTED_DOWNSAMPLING_FACTOR, 1); /* The device can sustain the full request rate */
        retval = FIELD_DP64(retval, CXL_CHMU_CAP2_0, CAP_FLAGS, 0x2); /* Always-on reporting mode */
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_CAP3_0:
	retval = FIELD_DP64(retval, CXL_CHMU_CAP3_0,
                            RANGE_CFG_BITMAP_REG_OFFSET, 0x70);
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_CAP4_0:
        retval = FIELD_DP64(retval, CXL_CHMU_CAP4_0,
                            HOTLIST_REG_OFFSET, 0x80); /* 16GB device */
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_CFG1_0:
        retval = FIELD_DP64(retval, CXL_CHMU_CFG1_0, M2S_REQ_TO_TRACK,
                            chmu->cfg[0].m2s_req_to_track);
        retval = FIELD_DP64(retval, CXL_CHMU_CFG1_0, FLAGS, chmu->cfg[0].flags);
        retval = FIELD_DP64(retval, CXL_CHMU_CFG1_0, CONTROL,
                            chmu->cfg[0].control);
        retval = FIELD_DP64(retval, CXL_CHMU_CFG1_0, HOTNESS_THRESHOLD,
                            chmu->cfg[0].hotness_threshold);
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_CFG2_0:
        retval = FIELD_DP64(retval, CXL_CHMU_CFG2_0, UNIT_SIZE,
                            chmu->cfg[0].unit_size);
        retval = FIELD_DP64(retval, CXL_CHMU_CFG2_0, DOWN_SAMPLING_FACTOR,
                            chmu->cfg[0].down_sampling_factor);
        retval = FIELD_DP64(retval, CXL_CHMU_CFG2_0, REPORTING_MODE,
                            chmu->cfg[0].reporting_mode);
        retval = FIELD_DP64(retval, CXL_CHMU_CFG2_0, EPOCH_LEN,
                            chmu->cfg[0].epoch_len);
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_CFG3_0:
        retval = FIELD_DP64(retval, CXL_CHMU_CFG3_0, HOTLIST_NOTI_THRESHOLD,
                            chmu->cfg[0].hotlist_noti_threshold);
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_STAT0:
        retval = FIELD_DP64(retval, CXL_CHMU_STAT0, STATUS,
                            chmu->stat[0].status);
        retval = FIELD_DP64(retval, CXL_CHMU_STAT0, OP_IN_PROGRESS,
                            chmu->stat[0].op_in_progress);
        retval = FIELD_DP64(retval, CXL_CHMU_STAT0, COUNTER_WIDTH,
                            chmu->stat[0].counter_width);
        retval = FIELD_DP64(retval, CXL_CHMU_STAT0, OVERFLOW_INT_STATUS,
                            chmu->stat[0].overflow_int_status);
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
    case A_CXL_CHMU_HOTLIST_HEAD_TAIL0:
        retval = FIELD_DP64(retval, CXL_CHMU_HOTLIST_HEAD_TAIL0, HOTLIST_HEAD,
                            chmu->hotlist_head_idx[0]);
        retval = FIELD_DP64(retval, CXL_CHMU_HOTLIST_HEAD_TAIL0, HOTLIST_TAIL,
                            chmu->hotlist_tail_idx[0]);
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
        break;
#define GET_BITMAP0_CASE(n)                                                    \
    case A_CXL_CHMU_RANGE_CFG_BITMAP##n##_0:                               \
        retval = FIELD_DP64(retval, CXL_CHMU_RANGE_CFG_BITMAP##n##_0,      \
                            BITMAP, chmu->range_bitmap[0][n]);             \
        fprintf(stderr, "offset: 0x%lx, size: %d, retval: 0x%lx\n",        \
                offset, size, retval);                                     \
        break;
    GET_BITMAP0_CASE(0);
    GET_BITMAP0_CASE(1);
    default:
        break;
    }

    if (!retval && A_CXL_CHMU_HOTLIST0 <= offset &&
        offset < A_CXL_CHMU_HOTLIST0 + CXL_HMU_HOTLIST_LEN * CXL_HMU_HOTLIST_SIZE) {
        int idx = (offset - A_CXL_CHMU_HOTLIST0) / CXL_HMU_HOTLIST_LEN;
        retval = FIELD_DP64(retval, CXL_CHMU_HOTLIST0, HOTLIST_ENTRY,
                chmu->hotlist[0][idx]);
        fprintf(stderr, "entry: offset: 0x%lx, size: %d, retval: 0x%lx\n", offset, size, retval);
    }

    return retval;
}

static void chmu_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    CHMUState *chmu = opaque;

    /* Lazy and assume correct size reads and writes for now  */
    switch (offset) {
    case A_CXL_CHMU_CFG1_0:
        chmu->cfg[0].m2s_req_to_track =
            FIELD_EX64(value, CXL_CHMU_CFG1_0, M2S_REQ_TO_TRACK);
        /* TODO check support bit before setting the value */
        chmu->cfg[0].flags =
            FIELD_EX64(value, CXL_CHMU_CFG1_0, FLAGS);
        chmu->cfg[0].control =
            FIELD_EX64(value, CXL_CHMU_CFG1_0, CONTROL);
        chmu->cfg[0].hotness_threshold =
            FIELD_EX64(value, CXL_CHMU_CFG1_0, HOTNESS_THRESHOLD);
        fprintf(stderr, "WR: CXL_CHMU_CFG1_0, offset: 0x%lx, size: %d\n", offset, size);
        break;
    case A_CXL_CHMU_CFG2_0:
        chmu->cfg[0].unit_size =
            FIELD_EX64(value, CXL_CHMU_CFG2_0, UNIT_SIZE);
        chmu->stat[0].counter_width = chmu->cfg[0].unit_size;
        chmu->cfg[0].down_sampling_factor =
            FIELD_EX64(value, CXL_CHMU_CFG2_0, DOWN_SAMPLING_FACTOR);
        chmu->cfg[0].reporting_mode =
            FIELD_EX64(value, CXL_CHMU_CFG2_0, REPORTING_MODE);
        chmu->cfg[0].epoch_len =
            FIELD_EX64(value, CXL_CHMU_CFG2_0, EPOCH_LEN);
        fprintf(stderr, "WR: CXL_CHMU_CFG2_0, offset: 0x%lx, size: %d\n", offset, size);
        break;
    case A_CXL_CHMU_CFG3_0:
        chmu->cfg[0].hotlist_noti_threshold =
            FIELD_EX64(value, CXL_CHMU_CFG3_0, HOTLIST_NOTI_THRESHOLD);
        fprintf(stderr, "WR: CXL_CHMU_CFG2_0, offset: 0x%lx, size: %d\n", offset, size);
        break;
    case A_CXL_CHMU_STAT0:
        /* Status, Operation in progress, Counter width are read-only */
        chmu->stat[0].overflow_int_status =
            FIELD_EX64(value, CXL_CHMU_STAT0, OVERFLOW_INT_STATUS);
        fprintf(stderr, "WR: CXL_CHMU_STAT0, offset: 0x%lx, size: %d\n", offset, size);
        break;
    case A_CXL_CHMU_HOTLIST_HEAD_TAIL0:
        chmu->hotlist_head_idx[0] = FIELD_EX32(value,
                                               CXL_CHMU_HOTLIST_HEAD_TAIL0,
                                               HOTLIST_HEAD);
        chmu->hotlist_tail_idx[0] = FIELD_EX32(value,
                                               CXL_CHMU_HOTLIST_HEAD_TAIL0,
                                               HOTLIST_TAIL);
        break;
#define WRITE_BITMAP0_CASE(n)                                              \
    case A_CXL_CHMU_RANGE_CFG_BITMAP##n##_0:                               \
        chmu->range_bitmap[0][n] =                                         \
            FIELD_EX64(value, CXL_CHMU_RANGE_CFG_BITMAP##n##_0, BITMAP);   \
        fprintf(stderr, "WR: bitmap%d, offset: 0x%lx, size: %d\n",         \
                n, offset, size);                                          \
        break;
    WRITE_BITMAP0_CASE(0);
    WRITE_BITMAP0_CASE(1);
    default:
        break;
    }
}

static const MemoryRegionOps chmu_ops = {
    .read = chmu_read,
    .write = chmu_write,
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

static void chmu_hotlist_update(void *opaque)
{
    CHMUState *chmu = opaque;
    PCIDevice *pdev = PCI_DEVICE(chmu->private);

    timer_del(chmu->timer);

#define CHMU_NUM_CHMUS  1
    for (int i = 0; i < CHMU_NUM_CHMUS; i++) {
        if (chmu->cfg[i].control & CXL_CHMU_CFG_CONTROL_ENABLE) {
            chmu->stat[i].status = 0x1;
        } else {
            chmu->stat[i].status = 0x0;
            continue;
        }

        uint16_t head = chmu->hotlist_head_idx[i];
        uint16_t tail = chmu->hotlist_tail_idx[i];
        if (head == (tail + 1) % CXL_HMU_HOTLIST_SIZE) {
            if (chmu->stat[i].overflow_int_status & CXL_CHMU_STAT_OVERFLOW_INT_OVERFLOW) {
                continue;
            }
            chmu->stat[i].overflow_int_status |= CXL_CHMU_STAT_OVERFLOW_INT_OVERFLOW;

            if (msix_enabled(pdev))
                msix_notify(pdev, chmu->msi_n[i]);
            else if (msi_enabled(pdev))
                msi_notify(pdev, chmu->msi_n[i]);

            fprintf(stderr, "hmu%d hotlist is full\n", i);
        } else {
            /* make hotlist entry */
            uint64_t *new_entry = &chmu->hotlist[i][tail];;
            uint64_t unit_id = chmu_get_random(34 - chmu->cfg[i].unit_size); /* 1 << 34 == 16GB */
            uint64_t counter_value = 0;
            if (chmu->cfg[i].reporting_mode == CXL_CHMU_REPORTING_MODE_EPOCH)
                counter_value = chmu_get_random(chmu->stat[i].counter_width);

            fprintf(stderr, "hmu%d: unit_id: 0x%lx, counter_value: 0x%lx\n",
                    i, unit_id, counter_value);
            if (!is_tracked(chmu, unit_id, chmu->stat[i].counter_width))
                continue;

            *new_entry = (unit_id << chmu->stat[i].counter_width) + counter_value;
            fprintf(stderr, "entry value: 0x%lx\n", *new_entry);

            chmu->hotlist_tail_idx[i] += 1;
            if (chmu->hotlist_tail_idx[i] == CXL_HMU_HOTLIST_SIZE)
                chmu->hotlist_tail_idx[i] = 0;
        }

        fprintf(stderr, "hmu%d: hotlist tail index: %d\n",
                i, chmu->hotlist_tail_idx[i]);
    }

    timer_mod(chmu->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000000);
}

void cxl_chmu_register_block_init(Object *obj, CXLDeviceState *cxl_dstate,
                                  int id, uint8_t msi_n)
{
    CHMUState *chmu = &cxl_dstate->chmu[id];
    MemoryRegion *registers = &cxl_dstate->chmu_registers[id];

    g_autofree gchar *name = g_strdup_printf("chmu%d-registers", id);

    chmu->msi_n[0] = msi_n;
    chmu->private = obj;
    memory_region_init_io(registers, obj, &chmu_ops, chmu,
                          name, pow2ceil(CXL_CHMU_SIZE));
    chmu->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, chmu_hotlist_update,
                               &cxl_dstate->chmu[id]);
    timer_mod(chmu->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000000);

    /* Need to force 64k Alignment in the bar */
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_CHMU_OFFSET(id), registers);
}

