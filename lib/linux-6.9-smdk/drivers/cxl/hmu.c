// SPDX-License-Identifier: GPL-2.0-only

/*
 * CXL 3.1 specification ECN CXL Hotness Monitoring Unit v0.2
 */

#define DEBUG

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/perf_event.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/bits.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/pci.h>

#include "cxlpci.h"
#include "cxlmem.h"
#include "cxl.h"
#include "hmu.h"

static int cxl_hmu_parse_caps(struct device *dev, struct cxl_hmu_info *info)
{
	void __iomem *base = info->base;
	u64 val, offset;
	int i;

	/* CHMU Common Capabilities */
	val = readq(base + CXL_HMU_COMMON_CAP1_REG);
	dev_info(dev, "offset: 0x%x, val: 0x%llx\n", CXL_HMU_COMMON_CAP1_REG, val);
	info->version = FIELD_GET(CXL_HMU_COMMON_CAP1_VERSION_MSK, val);
	if (info->version != 1) {
		dev_err(dev, "Wrong CHMU version: %d\n", info->version);
		return -ENODEV;
	}
	dev_info(dev, "hmu version: %d\n", info->version);

	info->num_chmus = FIELD_GET(CXL_HMU_COMMON_CAP1_CHMU_NUM_MSK, val);
	if (info->num_chmus > CXL_HMU_MAX_CNT) {
		dev_err(dev, "Wrong CHMU instance number: %d\n", info->num_chmus);
		return -ENODEV;
	}
	dev_info(dev, "hmu number of CHMUs: %d\n", info->num_chmus);

	val = readq(base + CXL_HMU_COMMON_CAP2_REG);
	info->instance_len = FIELD_GET(CXL_HMU_COMMON_CAP2_INSTANCE_LEN_MSK, val);
	dev_info(dev, "hmu instance length: %d\n", info->instance_len);

	for (i = 0; i < info->num_chmus; i++) {
		/* 1. CHMU Capability */
		offset = CXL_HMU_CAP1_REG + CXL_HMU_INSTANCE_OFFSET(info, i) ;
		val = readq(base + offset);
		dev_info(dev, "offset: 0x%llx, val: 0x%llx\n", offset, val);

		info->caps[i].hotlist_overflow_sup =
			FIELD_GET(CXL_HMU_CAP1_HOTLIST_OVERFLOW_SUP, val);
		info->caps[i].hotlist_levels_crossing_sup =
			FIELD_GET(CXL_HMU_CAP1_HOTLIST_LEVELS_CROSSING_SUP, val);

		/* FIXME Hot about 'Interrupt on Hotlist Levels Crossing Support? */
		if (info->caps[i].hotlist_overflow_sup)
			info->caps[i].irq = FIELD_GET(CXL_HMU_CAP1_INT_MSG_NUM_MSK, val);
		else
			info->caps[i].irq = -1;
		dev_info(dev, "hmu irq: %d\n", info->caps[i].irq);

		info->caps[i].epoch_type = FIELD_GET(CXL_HMU_CAP1_EPOCH_TYPE_MSK, val);
		dev_info(dev, "hmu epoch_type: %d\n", info->caps[i].epoch_type);

		info->caps[i].tracked_m2s_req =
			FIELD_GET(CXL_HMU_CAP1_TRACKED_M2S_REQ_MSK, val);
		dev_info(dev, "hmu tracked m2s requests: 0x%x\n",
			   info->caps[i].tracked_m2s_req);

		/* TODO {min,max}_epoch_len calculation */
		info->caps[i].max_epoch_len =
			FIELD_GET(CXL_HMU_CAP1_MAX_EPOCH_LEN_MSK, val);
		info->caps[i].min_epoch_len =
			FIELD_GET(CXL_HMU_CAP1_MIN_EPOCH_LEN_MSK, val);

		dev_info(dev, "hmu {max,min} epoch length: 0x%x 0x%x\n",
				info->caps[i].max_epoch_len, info->caps[i].min_epoch_len);

		info->caps[i].hotlist_size =
			FIELD_GET(CXL_HMU_CAP1_HOTLIST_SIZE_MSK, val);
		dev_info(dev, "hmu hotlist size: %d\n", info->caps[i].hotlist_size);

		/* 2. CHMU Capability */
		offset = CXL_HMU_CAP2_REG + CXL_HMU_INSTANCE_OFFSET(info, i);
		val = readq(base + offset);
		dev_info(dev, "offset: 0x%llx, val: 0x%llx\n", offset, val);

		info->caps[i].supported_unit_size =
			FIELD_GET(CXL_HMU_CAP2_UNIT_SIZE_SUP_MSK, val);
		dev_info(dev, "hmu supported unit size: 0x%x\n",
				info->caps[i].supported_unit_size);

		info->caps[i].supported_down_sampling_factor =
			FIELD_GET(CXL_HMU_CAP2_DOWNSAMPLING_SUP_MSK, val);
		dev_info(dev, "hmu supported down sampling factori: 0x%x\n",
				info->caps[i].supported_down_sampling_factor);

		info->caps[i].flags = FIELD_GET(CXL_HMU_CAP2_CAP_FLAGS_MSK, val);
		dev_info(dev, "hmu capability flags: 0x%x\n", info->caps[i].flags);

		/* 3. CHMU Capability */
		offset = CXL_HMU_CAP3_REG + CXL_HMU_INSTANCE_OFFSET(info, i);
		val = readq(base + offset);
		dev_info(dev, "offset: 0x%llx, val: 0x%llx\n", offset, val);

		info->caps[i].range_conf_bitmap_reg =
			FIELD_GET(CXL_HMU_CAP3_RANGE_CONF_BITMAP_REG_OFFSET_MSK, val);
		dev_info(dev, "hmu range conf bitmap register offset: 0x%llx\n",
				info->caps[i].range_conf_bitmap_reg);

		/* 4. CHMU Capability */
		offset = CXL_HMU_CAP4_REG + CXL_HMU_INSTANCE_OFFSET(info, i);
		val = readq(base + offset);
		dev_info(dev, "offset: 0x%llx, val: 0x%llx\n", offset, val);

		info->caps[i].hotlist_reg =
			FIELD_GET(CXL_HMU_CAP4_HOTLIST_REG_OFFSET_MSK, val);
		dev_info(dev, "hmu hotlist register offset: 0x%llx\n",
				info->caps[i].hotlist_reg);
	}

	return 0;
}

static irqreturn_t cxl_hmu_irq(int irq, void *data)
{
	struct cxl_hmu_info *info = data;
	void __iomem *base = info->base;
	struct pci_dev *pdev = to_pci_dev(info->dev->parent);
	struct cxl_dev_state *cxlds = pci_get_drvdata(pdev);
	struct cxl_memdev *cxlmd = cxlds->cxlmd;
	int i, chmu_idx = -1;
	uint16_t head, tail, buf_head, buf_tail;
	u64 val, entry;
	u64 idx, dpa, hpa;
	bool full_check = 0;

	dev_dbg(&cxlmd->dev, "hmu interrupt: %d\n", irq);

	for (i = 0; i < info->num_chmus; i++) {
		if (info->caps[i].irq != irq)
			continue;
		chmu_idx = i;

		dev_dbg(&cxlmd->dev, "hmu%d hotlist register: 0x%llx\n",
				i, info->caps[i].hotlist_reg);

		val = readq(base + CXL_HMU_STAT_REG + CXL_HMU_INSTANCE_OFFSET(info, i));
		info->status[i].counter_width =
			FIELD_GET(CXL_HMU_STAT_COUNTER_WIDTH_MSK, val);
		info->status[i].overflow_itr_status =
			FIELD_GET(CXL_HMU_STAT_OVERFLOW_INT_MSK, val);

		val = readq(base + CXL_HMU_CFG2_REG + CXL_HMU_INSTANCE_OFFSET(info, i));
		info->config[i].unit_size = FIELD_GET(CXL_HMU_CFG2_UNIT_SIZE_MSK, val);

		if (!(info->status[i].overflow_itr_status &
					CXL_HMU_STAT_HOTLIST_OVERFLOW)) {
			dev_warn(&cxlmd->dev, "Hotlist is not overflowed.");
			continue;
		}

		/* 1) read head, tail
		 * 2) read 64bit register from info->caps[i].hotlist_reg + head;
		 * 3) write (head + 1) to head
		 * 4) if head == tail, stop
		 * 5) Clear CHMU Stat Overflow Interrupt Status bit
		 */

		/* 1) read head, tail */
		spin_lock(&info->hot_buf_lock);
		val = readq(base + CXL_HMU_HOTLIST_HEAD_TAIL_REG +
				CXL_HMU_INSTANCE_OFFSET(info, i));
		head = FIELD_GET(CXL_HMU_HOTLIST_HEAD_MSK, val);
		tail = FIELD_GET(CXL_HMU_HOTLIST_TAIL_MSK, val);
		buf_head = info->hot_buf_head;
		buf_tail = info->hot_buf_tail;

		dev_dbg(&cxlmd->dev, "head: %d, tail: %d\n", head, tail);

		full_check = 0;
		while (head != tail) {
			if (buf_head == ((buf_tail + 1) % 1024)) {
				head += 1;
				head %= info->caps[i].hotlist_size;
				if (!full_check) {
					pr_info("Driver's hotlist buffer is full\n");
					full_check = 1;
				}
				continue;
			}

			/* 2) read 64bit register from info->caps[i].hotlist_reg + head; */
			entry = readq(base + CXL_HMU_COMMON_CAP_SIZE +
					info->caps[i].hotlist_reg + head * 8);
			idx = entry >> info->status[i].counter_width;
			dpa = idx * (1 << info->config[i].unit_size);
			hpa = cxl_memdev_dpa_to_hpa(cxlmd, dpa);

			if (hpa == ULLONG_MAX)
				dev_err(&cxlmd->dev, "Bad dpa-to-hpa translation\n");
			else {
				dev_dbg(&cxlmd->dev, "dpa: 0x%llx, hpa: 0x%llx\n", dpa, hpa);
				info->hot_buf[buf_tail] = hpa;
				buf_tail += 1;
				buf_tail %= 1024;
			}

			head += 1;
			head %= info->caps[i].hotlist_size;
			/* 3) if head == tail, stop */
		}

		/* 4) write device's head and tail */
		val = 0;
		val |= FIELD_PREP(CXL_HMU_HOTLIST_HEAD_MSK, head);
		val |= FIELD_PREP(CXL_HMU_HOTLIST_TAIL_MSK, tail);
		writeq(val, base + CXL_HMU_HOTLIST_HEAD_TAIL_REG +
				CXL_HMU_INSTANCE_OFFSET(info, i));

		val = readq(base + CXL_HMU_HOTLIST_HEAD_TAIL_REG +
				CXL_HMU_INSTANCE_OFFSET(info, i));
		head = FIELD_GET(CXL_HMU_HOTLIST_HEAD_MSK, val);
		tail = FIELD_GET(CXL_HMU_HOTLIST_TAIL_MSK, val);

		info->hot_buf_head = buf_head;
		info->hot_buf_tail = buf_tail;

		pr_info("Device's head: %d, tail: %d\n", head, tail);

		/* 5) Clear CHMU Stat Overflow Interrupt Status bit */
		val = readq(base + CXL_HMU_STAT_REG + CXL_HMU_INSTANCE_OFFSET(info, i));
		val &= ~(FIELD_PREP(CXL_HMU_STAT_OVERFLOW_INT_MSK,
			CXL_HMU_STAT_HOTLIST_OVERFLOW));
		info->status[i].overflow_itr_status =
			FIELD_GET(CXL_HMU_STAT_OVERFLOW_INT_MSK, val);

		writeq(val, base + CXL_HMU_STAT_REG + CXL_HMU_INSTANCE_OFFSET(info, i));
		spin_unlock(&info->hot_buf_lock);
	}

	if (chmu_idx == -1) {
		pr_err("irq number is not matched\n");
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int cxl_hmu_probe(struct device *dev)
{
	struct cxl_hmu *hmu = to_cxl_hmu(dev);
	struct pci_dev *pdev = to_pci_dev(dev->parent);
	struct cxl_hmu_info *info;
	char *irq_name;
	char *dev_name;
	int rc, irq[CXL_HMU_MAX_CNT], i;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	dev_set_drvdata(dev, info);

	info->base = hmu->base;
	info->dev = dev;

	rc = cxl_hmu_parse_caps(dev, info);
	if (rc)
		return rc;

	switch (hmu->type) {
	case CXL_HMU_MEMDEV:
		dev_name = devm_kasprintf(dev, GFP_KERNEL, "cxl_hmu_mem%d.%d",
				hmu->assoc_id, hmu->index);
		break;
	}
	if (!dev_name)
		return -ENOMEM;

	for (i = 0; i < info->num_chmus; i++) {
		if (info->caps[i].irq <= 0)
			return -EINVAL;

		rc = pci_irq_vector(pdev, info->caps[i].irq);
		if (rc < 0)
			return rc;
		irq[i] = rc;

		irq_name = devm_kasprintf(dev, GFP_KERNEL, "%s_overflow%d\n",
				dev_name, i);
		if (!irq_name)
			return -ENOMEM;

		rc = devm_request_irq(dev, irq[i], cxl_hmu_irq,
			   IRQF_SHARED | IRQF_ONESHOT, irq_name, info);
		if (rc)
			return rc;
		info->caps[i].irq = irq[i];
	}

	spin_lock_init(&info->hot_buf_lock);
	return 0;
}

static struct cxl_driver cxl_hmu_driver = {
	.name = "cxl_hmu",
	.probe = cxl_hmu_probe,
	.id = CXL_DEVICE_HMU,
};

module_cxl_driver(cxl_hmu_driver);

MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_HMU);
