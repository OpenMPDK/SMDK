// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022, Advanced Micro Devices, Inc.

#include <linux/cper.h>
#include <linux/cxl_err.h>

#define PROT_ERR_VALID_AGENT_TYPE		BIT_ULL(0)
#define PROT_ERR_VALID_AGENT_ADDRESS		BIT_ULL(1)
#define PROT_ERR_VALID_DEVICE_ID		BIT_ULL(2)
#define PROT_ERR_VALID_SERIAL_NUMBER		BIT_ULL(3)
#define PROT_ERR_VALID_CAPABILITY		BIT_ULL(4)
#define PROT_ERR_VALID_DVSEC			BIT_ULL(5)
#define PROT_ERR_VALID_ERROR_LOG		BIT_ULL(6)

#define PROT_ERR_FUNCTION(bits)			((bits) & GENMASK(7, 0))
#define PROT_ERR_DEVICE(bits)			(((bits) >> 8) & GENMASK(7, 0))
#define PROT_ERR_BUS(bits)			(((bits) >> 16) & GENMASK(7, 0))
#define PROT_ERR_SEGMENT(bits)			(((bits) >> 24) & GENMASK(7, 0))

static const char * const prot_err_agent_type_strs[] = {
	"CXL 1.1 device",
	"CXL 1.1 host downstream port",
};

enum {
	DEVICE,
	HOST_DOWNSTREAM_PORT,
};

void cper_print_prot_err(const char *pfx, const struct 
cper_sec_prot_err *prot_err) {
	if (prot_err->validation_bits & PROT_ERR_VALID_AGENT_TYPE)
		printk("%sagent_type: %d, %s\n", pfx, prot_err->agent_type,
		       prot_err->agent_type < ARRAY_SIZE(prot_err_agent_type_strs) ?
		       prot_err_agent_type_strs[prot_err->agent_type] : "unknown");
	if (prot_err->validation_bits & PROT_ERR_VALID_AGENT_ADDRESS) {
		switch (prot_err->agent_type) {
		case DEVICE:
			printk("%sagent_address: %04llx:%02llx:%02llx.%llx\n",
			       pfx, PROT_ERR_SEGMENT(prot_err->agent_addr),
			       PROT_ERR_BUS(prot_err->agent_addr),
			       PROT_ERR_DEVICE(prot_err->agent_addr),
			       PROT_ERR_FUNCTION(prot_err->agent_addr));
			break;
		case HOST_DOWNSTREAM_PORT:
			printk("%srcrb_base_address: 0x%016llx\n", pfx,
			       prot_err->agent_addr);
		}
	}
	if (prot_err->validation_bits & PROT_ERR_VALID_DEVICE_ID) {
		const __u8 *class_code;

		switch (prot_err->agent_type) {
		case DEVICE:
			printk("%sslot: %d\n", pfx, prot_err->device_id.slot >>
				CPER_PCIE_SLOT_SHIFT);
			printk("%svendor_id: 0x%04x, device_id: 0x%04x\n", pfx,
				prot_err->device_id.vendor_id, prot_err->device_id.device_id);
			printk("%ssub_vendor_id: 0x%04x, sub_device_id: 0x%04x\n", pfx,
				prot_err->device_id.sub_vendor_id,
				prot_err->device_id.sub_device_id);
			class_code = prot_err->device_id.class_code;
			printk("%sclass_code: %02x%02x\n", pfx, class_code[1], class_code[0]);
		}
	}
	if (prot_err->validation_bits & PROT_ERR_VALID_SERIAL_NUMBER) {
		switch (prot_err->agent_type) {
		case DEVICE:
			printk("%slower_dw: 0x%08x, upper_dw: 0x%08x\n", pfx,
				prot_err->dev_serial_num.lower_dw,
				prot_err->dev_serial_num.upper_dw);
		}
	}
	if (prot_err->validation_bits & PROT_ERR_VALID_ERROR_LOG) {
		struct ras_capability_regs *cxl_ras;
		int size = sizeof(*prot_err) + prot_err->dvsec_len;

		printk("%sError log length: 0x%04x\n", pfx, prot_err->err_len);

		printk("%sCXL Protocol Error Log:\n", pfx);
		cxl_ras = (struct ras_capability_regs *)((long)prot_err + size);
		printk("%scxl_ras_uncor_status: 0x%08x, cxl_ras_uncor_mask: 0x%08x\n",
			pfx, cxl_ras->uncor_status, cxl_ras->uncor_mask);
		printk("%scxl_ras_uncor_severity: 0x%08x\n", pfx, cxl_ras->uncor_severity);
		printk("%scxl_ras_cor_status: 0x%08x, cxl_ras_cor_mask: 0x%08x\n",
			pfx, cxl_ras->cor_status, cxl_ras->cor_mask);
		printk("%sHeader Log Registers:\n", pfx);
		print_hex_dump(pfx, "", DUMP_PREFIX_OFFSET, 16, 4, cxl_ras->header_log,
			       sizeof(cxl_ras->header_log), 0);
	}
}
