#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci-bridge/cxl_upstream_port.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "hw/cxl/cxl.h"

struct CSWMBCCIClass {
    PCIDeviceClass parent_class;
};

static void cswmbcci_reset(DeviceState *dev)
{
    CSWMBCCIDev *cswmb = CXL_SWITCH_MAILBOX_CCI(dev);
    cxl_device_register_init_swcci(cswmb);
}

static void cswbcci_realize(PCIDevice *pci_dev, Error **errp)
{
    CSWMBCCIDev *cswmb = CXL_SWITCH_MAILBOX_CCI(pci_dev);
    CXLComponentState *cxl_cstate = &cswmb->cxl_cstate;
    CXLDeviceState *cxl_dstate = &cswmb->cxl_dstate;
    CXLDVSECRegisterLocator *regloc_dvsec;
    CXLUpstreamPort *usp;

    if (!cswmb->target) {
        error_setg(errp, "Target not set");
        return;
    }
    usp = CXL_USP(cswmb->target);

    pcie_endpoint_cap_init(pci_dev, 0x80);
    cxl_cstate->dvsec_offset = 0x100;
    cxl_cstate->pdev = pci_dev;
    cswmb->cci = &usp->swcci;
    cxl_device_register_block_init(OBJECT(pci_dev), cxl_dstate, cswmb->cci);
    pci_register_bar(pci_dev, 0,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &cxl_dstate->device_registers);
    regloc_dvsec = &(CXLDVSECRegisterLocator) {
        .rsvd         = 0,
        .reg_base[0].lo = RBI_CXL_DEVICE_REG | 0,
        .reg_base[0].hi = 0,
    };
    cxl_component_create_dvsec(cxl_cstate, CXL3_SWITCH_MAILBOX_CCI,
                               REG_LOC_DVSEC_LENGTH, REG_LOC_DVSEC,
                               REG_LOC_DVSEC_REVID, (uint8_t *)regloc_dvsec);

    cxl_initialize_mailbox_swcci(cswmb->cci, DEVICE(pci_dev),
                                 DEVICE(cswmb->target),
                                 CXL_MAILBOX_MAX_PAYLOAD_SIZE);
}

static void cswmbcci_exit(PCIDevice *pci_dev)
{
}

static Property cxl_switch_cci_props[] = {
    DEFINE_PROP_LINK("target", CSWMBCCIDev,
                     target, TYPE_CXL_USP, PCIDevice *),
    DEFINE_PROP_END_OF_LIST(),
};

static void cswmbcci_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = cswbcci_realize;
    pc->exit = cswmbcci_exit;
    pc->class_id = 0x0c0b; /* Serial bus, CXL Switch CCI */
    pc->vendor_id = 0x19e5;
    pc->device_id = 0xbeef; /* FIXME - assign a valid ID for this function */
    pc->revision = 0;
    dc->desc = "CXL Switch Mailbox CCI";
    dc->reset = cswmbcci_reset;
    device_class_set_props(dc, cxl_switch_cci_props);
}

static const TypeInfo cswmbcci_info = {
    .name = TYPE_CXL_SWITCH_MAILBOX_CCI,
    .parent = TYPE_PCI_DEVICE,
    .class_size = sizeof(struct CSWMBCCIClass),
    .class_init = cswmbcci_class_init,
    .instance_size = sizeof(CSWMBCCIDev),
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void cxl_switch_mailbox_cci_register(void)
{
    type_register_static(&cswmbcci_info);
}
type_init(cxl_switch_mailbox_cci_register);
