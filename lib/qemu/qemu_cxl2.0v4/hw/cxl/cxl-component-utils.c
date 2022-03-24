/*
 * CXL Utility library for components
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/pci/pci.h"
#include "hw/cxl/cxl.h"

static uint64_t cxl_cache_mem_read_reg(void *opaque, hwaddr offset,
                                       unsigned size)
{
    CXLComponentState *cxl_cstate = opaque;
    ComponentRegisters *cregs = &cxl_cstate->crb;

    assert(size == 4);

    if (cregs->special_ops && cregs->special_ops->read) {
        return cregs->special_ops->read(cxl_cstate, offset, size);
    } else {
        return cregs->cache_mem_registers[offset / 4];
    }
}

static void cxl_cache_mem_write_reg(void *opaque, hwaddr offset, uint64_t value,
                                    unsigned size)
{
    CXLComponentState *cxl_cstate = opaque;
    ComponentRegisters *cregs = &cxl_cstate->crb;

    assert(size == 4);

    if (cregs->special_ops && cregs->special_ops->write) {
        cregs->special_ops->write(cxl_cstate, offset, value, size);
    } else {
        cregs->cache_mem_registers[offset / 4] = value;
    }
}

/*
 * 8.2.3
 *   The access restrictions specified in Section 8.2.2 also apply to CXL 2.0
 *   Component Registers.
 *
 * 8.2.2
 *   • A 32 bit register shall be accessed as a 4 Bytes quantity. Partial
 *   reads are not permitted.
 *   • A 64 bit register shall be accessed as a 8 Bytes quantity. Partial
 *   reads are not permitted.
 *
 * As of the spec defined today, only 4 byte registers exist.
 */
static const MemoryRegionOps cache_mem_ops = {
    .read = cxl_cache_mem_read_reg,
    .write = cxl_cache_mem_write_reg,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

void cxl_component_register_block_init(Object *obj,
                                       CXLComponentState *cxl_cstate,
                                       const char *type)
{
    ComponentRegisters *cregs = &cxl_cstate->crb;

    memory_region_init(&cregs->component_registers, obj, type,
                       CXL2_COMPONENT_BLOCK_SIZE);

    /* io registers controls link which we don't care about in QEMU */
    memory_region_init_io(&cregs->io, obj, NULL, cregs, ".io",
                          CXL2_COMPONENT_IO_REGION_SIZE);
    memory_region_init_io(&cregs->cache_mem, obj, &cache_mem_ops, cregs,
                          ".cache_mem", CXL2_COMPONENT_CM_REGION_SIZE);

    memory_region_add_subregion(&cregs->component_registers, 0, &cregs->io);
    memory_region_add_subregion(&cregs->component_registers,
                                CXL2_COMPONENT_IO_REGION_SIZE,
                                &cregs->cache_mem);
}

static void ras_init_common(uint32_t *reg_state)
{
    reg_state[R_CXL_RAS_UNC_ERR_STATUS] = 0;
    reg_state[R_CXL_RAS_UNC_ERR_MASK] = 0x1cfff;
    reg_state[R_CXL_RAS_UNC_ERR_SEVERITY] = 0x1cfff;
    reg_state[R_CXL_RAS_COR_ERR_STATUS] = 0;
    reg_state[R_CXL_RAS_COR_ERR_MASK] = 0x3f;
    reg_state[R_CXL_RAS_ERR_CAP_CTRL] = 0; /* CXL switches and devices must set */
}

static void hdm_init_common(uint32_t *reg_state)
{
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, DECODER_COUNT, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_GLOBAL_CONTROL, HDM_DECODER_ENABLE, 0);
}

void cxl_component_register_init_common(uint32_t *reg_state, enum reg_type type)
{
    int caps = 0;
    switch (type) {
    case CXL2_DOWNSTREAM_PORT:
    case CXL2_DEVICE:
        /* CAP, RAS, Link */
        caps = 2;
        break;
    case CXL2_UPSTREAM_PORT:
    case CXL2_TYPE3_DEVICE:
    case CXL2_LOGICAL_DEVICE:
        /* + HDM */
        caps = 3;
        break;
    case CXL2_ROOT_PORT:
        /* + Extended Security, + Snoop */
        caps = 5;
        break;
    default:
        abort();
    }

    memset(reg_state, 0, CXL2_COMPONENT_CM_REGION_SIZE);

    /* CXL Capability Header Register */
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, ID, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, VERSION, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, CACHE_MEM_VERSION, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, ARRAY_SIZE, caps);


#define init_cap_reg(reg, id, version)                                        \
    _Static_assert(CXL_##reg##_REGISTERS_OFFSET != 0, "Invalid cap offset\n");\
    do {                                                                      \
        int which = R_CXL_##reg##_CAPABILITY_HEADER;                          \
        reg_state[which] = FIELD_DP32(reg_state[which],                       \
                                      CXL_##reg##_CAPABILITY_HEADER, ID, id); \
        reg_state[which] =                                                    \
            FIELD_DP32(reg_state[which], CXL_##reg##_CAPABILITY_HEADER,       \
                       VERSION, version);                                     \
        reg_state[which] =                                                    \
            FIELD_DP32(reg_state[which], CXL_##reg##_CAPABILITY_HEADER, PTR,  \
                       CXL_##reg##_REGISTERS_OFFSET);                         \
    } while (0)

    init_cap_reg(RAS, 2, 1);
    ras_init_common(reg_state);

    init_cap_reg(LINK, 4, 2);

    if (caps < 3) {
        return;
    }

    init_cap_reg(HDM, 5, 1);
    hdm_init_common(reg_state);

    if (caps < 5) {
        return;
    }

    init_cap_reg(EXTSEC, 6, 1);
    init_cap_reg(SNOOP, 8, 1);

#undef init_cap_reg
}

/*
 * Helper to creates a DVSEC header for a CXL entity. The caller is responsible
 * for tracking the valid offset.
 *
 * This function will build the DVSEC header on behalf of the caller and then
 * copy in the remaining data for the vendor specific bits.
 */
void cxl_component_create_dvsec(CXLComponentState *cxl, uint16_t length,
                                uint16_t type, uint8_t rev, uint8_t *body)
{
    PCIDevice *pdev = cxl->pdev;
    uint16_t offset = cxl->dvsec_offset;

    assert(offset >= PCI_CFG_SPACE_SIZE &&
           ((offset + length) < PCI_CFG_SPACE_EXP_SIZE));
    assert((length & 0xf000) == 0);
    assert((rev & ~0xf) == 0);

    /* Create the DVSEC in the MCFG space */
    pcie_add_capability(pdev, PCI_EXT_CAP_ID_DVSEC, 1, offset, length);
    pci_set_long(pdev->config + offset + PCIE_DVSEC_HEADER1_OFFSET,
                 (length << 20) | (rev << 16) | CXL_VENDOR_ID);
    pci_set_word(pdev->config + offset + PCIE_DVSEC_ID_OFFSET, type);
    memcpy(pdev->config + offset + sizeof(struct dvsec_header),
           body + sizeof(struct dvsec_header),
           length - sizeof(struct dvsec_header));

    /* Update state for future DVSEC additions */
    range_init_nofail(&cxl->dvsecs[type], cxl->dvsec_offset, length);
    cxl->dvsec_offset += length;
}
