/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2023 MemVerge Inc.
 * Copyright (c) 2023 SK hynix Inc.
 */

#include <sys/shm.h>
#include "qemu/osdep.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_mailbox.h"
#include "hw/cxl/cxl_device.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/qdev-properties.h"
#include "skhynix_niagara.h"

#define TYPE_CXL_NIAGARA "cxl-skh-niagara"
OBJECT_DECLARE_TYPE(CXLNiagaraState, CXLNiagaraClass, CXL_NIAGARA)

/*
 * CXL r3.0 section 7.6.7.5.1 - Get Multi-Headed Info (Opcode 5500h)
 *
 * This command retrieves the number of heads, number of supported LDs,
 * and Head-to-LD mapping of a Multi-Headed device.
 */
static CXLRetCode niagara_mhd_get_info(const struct cxl_cmd *cmd,
                                       uint8_t *payload_in,
                                       size_t len_in,
                                       uint8_t *payload_out,
                                       size_t *len_out,
                                       CXLCCI * cci)
{
    CXLNiagaraState *s = CXL_NIAGARA(cci->d);
    NiagaraMHDGetInfoInput *input = (void *)payload_in;
    NiagaraMHDGetInfoOutput *output = (void *)payload_out;

    uint8_t start_ld = input->start_ld;
    uint8_t ldmap_len = input->ldmap_len;
    uint8_t i;

    if (start_ld >= s->mhd_state->nr_lds) {
        return CXL_MBOX_INVALID_INPUT;
    }

    output->nr_lds = s->mhd_state->nr_lds;
    output->nr_heads = s->mhd_state->nr_heads;
    output->resv1 = 0;
    output->start_ld = start_ld;
    output->resv2 = 0;

    for (i = 0; i < ldmap_len && (start_ld + i) < output->nr_lds; i++) {
        output->ldmap[i] = s->mhd_state->ldmap[start_ld + i];
    }
    output->ldmap_len = i;

    *len_out = sizeof(*output) + output->ldmap_len;
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_niagara_get_section_status(const struct cxl_cmd *cmd,
                                               uint8_t *payload_in,
                                               size_t len_in,
                                               uint8_t *payload_out,
                                               size_t *len_out,
                                               CXLCCI *cci)
{
    CXLNiagaraState *s = CXL_NIAGARA(cci->d);
    NiagaraSharedState *nss = (NiagaraSharedState *)s->mhd_state;
    NiagaraGetSectionStatusOutput *output = (void *)payload_out;

    output->total_section_count = nss->total_sections;
    output->free_section_count = nss->free_sections;

    *len_out = sizeof(*output);

    return CXL_MBOX_SUCCESS;
}

static bool niagara_claim_section(CXLNiagaraState *s,
                                  uint32_t *sections,
                                  uint32_t section_idx)
{
    uint32_t *section = &sections[section_idx];
    uint32_t old_value = __sync_fetch_and_or(section, (1 << s->mhd_head));

    /* if we already owned the section, we haven't claimed it */
    if (old_value & (1 << s->mhd_head)) {
        return false;
    }

    /* if the old value wasn't 0, this section was already claimed */
    if (old_value != 0) {
        __sync_fetch_and_and(section, ~(1 << s->mhd_head));
        return false;
    }
    return true;
}

static void niagara_release_sections(CXLNiagaraState *s,
                                     uint32_t *section_ids,
                                     uint32_t count)
{
    NiagaraSharedState *nss = s->mhd_state;
    uint32_t *sections = &nss->sections[0];
    uint32_t section;
    uint32_t old_val;
    uint32_t i;

    /* free any successfully allocated sections */
    for (i = 0; i < count; i++) {
        section = section_ids[i];
        old_val = __sync_fetch_and_and(&sections[section], ~(1 << s->mhd_head));

        if (old_val & (1 << s->mhd_head)) {
            __sync_fetch_and_add(&nss->free_sections, 1);
        }
    }
}

static void niagara_alloc_build_output(NiagaraAllocOutput *output,
                                       size_t *len_out,
                                       uint32_t *section_ids,
                                       uint32_t section_count)
{
    uint32_t extents;
    uint32_t previous;
    uint32_t i;

    /* Build the output */
    output->section_count = section_count;
    extents = 0;
    previous = 0;
    for (i = 0; i < section_count; i++) {
        if (i == 0) {
            /* start the first extent */
            output->extents[extents].start_section_id = section_ids[i];
            output->extents[extents].section_count = 1;
            extents++;
        } else if (section_ids[i] == (previous + 1)) {
            /* increment the current extent */
            output->extents[extents - 1].section_count++;
        } else {
            /* start a new extent */
            output->extents[extents].start_section_id = section_ids[i];
            output->extents[extents].section_count = 1;
            extents++;
        }
        previous = section_ids[i];
    }
    output->extent_count = extents;
    *len_out = 8 + (16 * extents);
    return;
}

static CXLRetCode niagara_alloc_manual(CXLNiagaraState *s,
                                       NiagaraAllocInput *input,
                                       NiagaraAllocOutput *output,
                                       size_t *len_out)
{
    NiagaraSharedState *nss = s->mhd_state;
    uint32_t cur_extent = 0;
    g_autofree uint32_t *section_ids = NULL;
    uint32_t *sections;
    uint32_t allocated;
    uint32_t i = 0;
    uint32_t ttl_sec = 0;

    /* input validation: iterate extents, count total sectios */
    for (i = 0; i < input->extent_count; i++) {
        uint32_t start = input->extents[i].start_section_id;
        uint32_t end = start + input->extents[i].section_count;

        if ((start >= nss->total_sections) ||
            (end > nss->total_sections)) {
            return CXL_MBOX_INVALID_INPUT;
        }
        ttl_sec += input->extents[i].section_count;
    }

    if (ttl_sec != input->section_count) {
        return CXL_MBOX_INVALID_INPUT;
    }

    section_ids = malloc(input->section_count * sizeof(uint32_t));
    sections = &nss->sections[0];
    allocated = 0;

    /* for each section requested in the input, try to allocate that section */
    for (cur_extent = 0; cur_extent < input->extent_count; cur_extent++) {
        uint32_t start_section = input->extents[cur_extent].start_section_id;
        uint32_t section_count = input->extents[cur_extent].section_count;
        uint32_t cur_section;

        for (cur_section = input->extents[cur_extent].start_section_id;
             cur_section < start_section + section_count;
             cur_section++) {
            if (niagara_claim_section(s, sections, cur_section)) {
                __sync_fetch_and_sub(&nss->free_sections, 1);
                section_ids[allocated++] = cur_section;
            }
        }
    }

    if ((input->policy & NIAGARA_SECTION_ALLOC_POLICY_ALL_OR_NOTHING) &&
        (allocated != input->section_count)) {
        niagara_release_sections(s, section_ids, allocated);
        return CXL_MBOX_INTERNAL_ERROR;
    }

    niagara_alloc_build_output(output, len_out, section_ids, allocated);
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode niagara_alloc_auto(CXLNiagaraState *s,
                                     NiagaraAllocInput *input,
                                     NiagaraAllocOutput *output,
                                     size_t *len_out)
{
    NiagaraSharedState *nss = s->mhd_state;
    g_autofree uint32_t *section_ids = NULL;
    uint32_t section_count = input->section_count;
    uint32_t total_sections = nss->total_sections;
    uint32_t *sections = &nss->sections[0];
    uint32_t allocated = 0;
    uint32_t cur_section;

    section_ids = malloc(section_count * sizeof(uint32_t));

    /* Iterate the the section list and allocate free sections */
    for (cur_section = 0;
         (cur_section < total_sections) && (allocated != section_count);
         cur_section++) {
        if (niagara_claim_section(s, sections, cur_section)) {
            __sync_fetch_and_sub(&nss->free_sections, 1);
            section_ids[allocated++] = cur_section;
        }
    }

    if ((input->policy & NIAGARA_SECTION_ALLOC_POLICY_ALL_OR_NOTHING) &&
        (allocated != input->section_count)) {
        niagara_release_sections(s, section_ids, allocated);
        return CXL_MBOX_INTERNAL_ERROR;
    }

    niagara_alloc_build_output(output, len_out, section_ids, allocated);
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_niagara_set_section_alloc(const struct cxl_cmd *cmd,
                                                uint8_t *payload_in,
                                                size_t len_in,
                                                uint8_t *payload_out,
                                                size_t *len_out,
                                                CXLCCI *cci)
{
    CXLNiagaraState *s = CXL_NIAGARA(cci->d);
    NiagaraAllocInput *input = (void *)payload_in;
    NiagaraAllocOutput *output = (void *)payload_out;

    if (input->section_count == 0 ||
        input->section_count > s->mhd_state->total_sections) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (input->policy & NIAGARA_SECTION_ALLOC_POLICY_MANUAL) {
        return niagara_alloc_manual(s, input, output, len_out);
    }

    return niagara_alloc_auto(s, input, output, len_out);
}

static CXLRetCode cmd_niagara_set_section_release(const struct cxl_cmd *cmd,
                                                uint8_t *payload_in,
                                                size_t len_in,
                                                uint8_t *payload_out,
                                                size_t *len_out,
                                                CXLCCI *cci)
{
    CXLNiagaraState *s = CXL_NIAGARA(cci->d);
    NiagaraSharedState *nss = s->mhd_state;
    NiagaraReleaseInput *input = (void *)payload_in;
    uint32_t i, j;
    uint32_t *sections = &nss->sections[0];

    for (i = 0; i < input->extent_count; i++) {
        uint32_t start = input->extents[i].start_section_id;

        for (j = 0; j < input->extents[i].section_count; j++) {
            uint32_t *cur_section = &sections[start + j];
            uint32_t hbit = 1 << s->mhd_head;
            uint32_t old_val = __sync_fetch_and_and(cur_section, ~hbit);

            if (old_val & hbit) {
                __sync_fetch_and_add(&nss->free_sections, 1);
            }
        }
    }
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_niagara_set_section_size(const struct cxl_cmd *cmd,
                                             uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out,
                                             CXLCCI *cci)
{
    CXLNiagaraState *s = CXL_NIAGARA(cci->d);
    NiagaraSharedState *nss = s->mhd_state;
    NiagaraSetSectionSizeInput *input = (void *)payload_in;
    NiagaraSetSectionSizeOutput *output = (void *)payload_out;
    uint32_t prev_size = nss->section_size;
    uint32_t prev_ttl = nss->total_sections;

    /* Only allow size change if all sections are free */
    if (nss->free_sections != nss->total_sections) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    if (nss->section_size != (1 << (input->section_unit - 1))) {
        nss->section_size = (1 << (input->section_unit - 1));
        nss->total_sections = (prev_size * prev_ttl) / nss->section_size;
        nss->free_sections = nss->total_sections;
    }

    output->section_unit = input->section_unit;
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_niagara_get_section_map(const struct cxl_cmd *cmd,
                                              uint8_t *payload_in,
                                              size_t len_in,
                                              uint8_t *payload_out,
                                              size_t *len_out,
                                              CXLCCI *cci)
{
    CXLNiagaraState *s = CXL_NIAGARA(cci->d);
    NiagaraSharedState *nss = s->mhd_state;
    NiagaraGetSectionMapInput *input = (void *)payload_in;
    NiagaraGetSectionMapOutput *output = (void *)payload_out;
    uint32_t *sections = &nss->sections[0];
    uint8_t query_type = input->query_type;
    uint32_t i;
    uint32_t bytes;

    if ((query_type != NIAGARA_GSM_QUERY_FREE) &&
        (query_type != NIAGARA_GSM_QUERY_ALLOCATED)) {
        return CXL_MBOX_INVALID_INPUT;
    }

    output->ttl_section_count = nss->total_sections;
    output->qry_section_count = 0;
    bytes = (output->ttl_section_count / 8);
    if (output->ttl_section_count % 8) {
        bytes += 1;
    }

    for (i = 0; i < bytes; i++) {
        output->bitset[i] = 0x0;
    }

    /* Iterate the the section list and check the bits */
    for (i = 0; (i < nss->total_sections); i++) {
        uint32_t section = sections[i];

        if (((query_type == NIAGARA_GSM_QUERY_FREE) && (!section)) ||
            ((query_type == NIAGARA_GSM_QUERY_ALLOCATED) &&
             (section & (1 << s->mhd_head)))) {
            uint32_t byte = i / 8;
            uint8_t bit = 1 << (i % 8);

            output->bitset[byte] |= bit;
            output->qry_section_count++;
        }
    }

    *len_out = 8 + bytes;
    return CXL_MBOX_SUCCESS;
}

static bool mhdsld_access_valid(PCIDevice *d,
                                uint64_t dpa_offset,
                                unsigned int size)
{
    CXLNiagaraState *s = CXL_NIAGARA(d);
    NiagaraSharedState *nss = s->mhd_state;
    uint32_t section = (dpa_offset / NIAGARA_MIN_MEMBLK);

    return nss->sections[section] & (1 << s->mhd_head);
}

static const struct cxl_cmd cxl_cmd_set_niagara[256][256] = {
    [NIAGARA_CMD][GET_SECTION_STATUS] = { "GET_SECTION_STATUS",
        cmd_niagara_get_section_status, 0, 0 },
    [NIAGARA_CMD][SET_SECTION_ALLOC] = { "SET_SECTION_ALLOC",
        cmd_niagara_set_section_alloc, ~0,
        (CXL_MBOX_IMMEDIATE_CONFIG_CHANGE | CXL_MBOX_IMMEDIATE_DATA_CHANGE |
         CXL_MBOX_IMMEDIATE_POLICY_CHANGE | CXL_MBOX_IMMEDIATE_LOG_CHANGE)
    },
    [NIAGARA_CMD][SET_SECTION_RELEASE] = { "SET_SECTION_RELEASE",
        cmd_niagara_set_section_release, ~0,
        (CXL_MBOX_IMMEDIATE_CONFIG_CHANGE | CXL_MBOX_IMMEDIATE_DATA_CHANGE |
         CXL_MBOX_IMMEDIATE_POLICY_CHANGE | CXL_MBOX_IMMEDIATE_LOG_CHANGE)
    },
    [NIAGARA_CMD][SET_SECTION_SIZE] = { "SET_SECTION_SIZE",
        cmd_niagara_set_section_size, 8,
        (CXL_MBOX_IMMEDIATE_CONFIG_CHANGE | CXL_MBOX_IMMEDIATE_DATA_CHANGE |
         CXL_MBOX_IMMEDIATE_POLICY_CHANGE | CXL_MBOX_IMMEDIATE_LOG_CHANGE)
    },
    [NIAGARA_CMD][GET_SECTION_MAP] = { "GET_SECTION_MAP",
        cmd_niagara_get_section_map, 8 , CXL_MBOX_IMMEDIATE_DATA_CHANGE },
};

static Property cxl_niagara_props[] = {
    DEFINE_PROP_UINT32("mhd-head", CXLNiagaraState, mhd_head, ~(0)),
    DEFINE_PROP_UINT32("mhd-shmid", CXLNiagaraState, mhd_shmid, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void cxl_niagara_realize(PCIDevice *pci_dev, Error **errp)
{
    CXLNiagaraState *s = CXL_NIAGARA(pci_dev);

    ct3_realize(pci_dev, errp);

    if (!s->mhd_shmid || s->mhd_head == ~(0)) {
        error_setg(errp, "is_mhd requires mhd_shmid and mhd_head settings");
        return;
    }

    if (s->mhd_head >= 32) {
        error_setg(errp, "MHD Head ID must be between 0-31");
        return;
    }

    s->mhd_state = shmat(s->mhd_shmid, NULL, 0);
    if (s->mhd_state == (void *)-1) {
        s->mhd_state = NULL;
        error_setg(errp, "Unable to attach MHD State. Check ipcs is valid");
        return;
    }

    /* For now, limit the number of LDs to the number of heads (SLD) */
    if (s->mhd_head >= s->mhd_state->nr_heads) {
        error_setg(errp, "Invalid head ID for multiheaded device.");
        return;
    }

    if (s->mhd_state->nr_lds <= s->mhd_head) {
        error_setg(errp, "MHD Shared state does not have sufficient lds.");
        return;
    }

    s->mhd_state->ldmap[s->mhd_head] = s->mhd_head;
    return;
}

static void cxl_niagara_exit(PCIDevice *pci_dev)
{
    CXLNiagaraState *s = CXL_NIAGARA(pci_dev);

    ct3_exit(pci_dev);

    if (s->mhd_state) {
        shmdt(s->mhd_state);
    }
}

static void cxl_niagara_reset(DeviceState *d)
{
    CXLNiagaraState *s = CXL_NIAGARA(d);

    ct3d_reset(d);
    cxl_add_cci_commands(&s->ct3d.cci, cxl_cmd_set_niagara, 512);
}

static void cxl_niagara_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    CXLType3Class *cvc = CXL_TYPE3_CLASS(klass);

    pc->realize = cxl_niagara_realize;
    pc->exit = cxl_niagara_exit;
    dc->reset = cxl_niagara_reset;
    device_class_set_props(dc, cxl_niagara_props);

    cvc->mhd_get_info = niagara_mhd_get_info;
    cvc->mhd_access_valid = mhdsld_access_valid;
}

static const TypeInfo cxl_niagara_info = {
    .name = TYPE_CXL_NIAGARA,
    .parent = TYPE_CXL_TYPE3,
    .class_size = sizeof(struct CXLNiagaraClass),
    .class_init = cxl_niagara_class_init,
    .instance_size = sizeof(CXLNiagaraState),
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CXL_DEVICE },
        { INTERFACE_PCIE_DEVICE },
        {}
    },
};

static void cxl_niagara_register_types(void)
{
    type_register_static(&cxl_niagara_info);
}

type_init(cxl_niagara_register_types)
