// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/io.h>
#include "cxlmem.h"
#include "pci.h"
#include "cxl.h"
#ifdef CONFIG_EXMEM
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/numa.h>

struct numa_memblk {
	u64 start;
	u64 end;
	int nid;
};

struct numa_meminfo {
	int nr_blks;
	struct numa_memblk blk[NR_NODE_MEMBLKS];
};

static struct numa_meminfo cxl_meminfo;

extern void numa_get_reserved_meminfo(struct numa_meminfo *mi);

static void print_numa_reserved_meminfo(struct numa_meminfo *mi)
{
	int i;

	pr_info("CXL: node count: %d\n", mi->nr_blks);

	for (i = 0; i < mi->nr_blks; i++) {
		if (mi->blk[i].nid == NUMA_NO_NODE)
			continue;

		pr_info("CXL: nid: %d, [mem %#010llx-%#010llx]\n",
				mi->blk[i].nid, mi->blk[i].start, mi->blk[i].end);
	}
}


static int __check_cxl_range(struct resource *res, void *data)
{
	int i = 0;

	if (res->parent != &iomem_resource) {
		pr_info("CXL: skip %pr, already claimed\n", res);
		return 0;
	}

	pr_info("CXL: soft reserved: [mem %#010llx-%#010llx]\n",
			res->start, res->end);

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (res->start <= cxl_meminfo.blk[i].start &&
				cxl_meminfo.blk[i].end <= res->end + 1)
			continue;

		pr_info("CXL: out of range: [mem %#010llx-%#010llx]\n",
				cxl_meminfo.blk[i].start, cxl_meminfo.blk[i].end);

		cxl_meminfo.blk[i].nid = NUMA_NO_NODE;
	}

	return 0;
}

static void check_reserved_meminfo(struct numa_meminfo *mi)
{
	walk_iomem_res_desc(IORES_DESC_SOFT_RESERVED,
			IORESOURCE_MEM, 0, -1, NULL, __check_cxl_range);
}
#endif

/**
 * DOC: cxl pci
 *
 * This implements the PCI exclusive functionality for a CXL device as it is
 * defined by the Compute Express Link specification. CXL devices may surface
 * certain functionality even if it isn't CXL enabled. While this driver is
 * focused around the PCI specific aspects of a CXL device, it binds to the
 * specific CXL memory device class code, and therefore the implementation of
 * cxl_pci is focused around CXL memory devices.
 *
 * The driver has several responsibilities, mainly:
 *  - Create the memX device and register on the CXL bus.
 *  - Enumerate device's register interface and map them.
 *  - Registers nvdimm bridge device with cxl_core.
 *  - Registers a CXL mailbox with cxl_core.
 */

#define cxl_doorbell_busy(cxlds)                                                \
	(readl((cxlds)->regs.mbox + CXLDEV_MBOX_CTRL_OFFSET) &                  \
	 CXLDEV_MBOX_CTRL_DOORBELL)

/* CXL 2.0 - 8.2.8.4 */
#define CXL_MAILBOX_TIMEOUT_MS (2 * HZ)

#ifdef CONFIG_EXMEM
enum CXL_MEM_MODE {
	CXL_MEM_MODE_SOFT_RESERVED = 0,
	CXL_MEM_MODE_NORMAL_ZONE,
	CXL_MEM_MODE_EXMEM_ZONE,
	CXL_MEM_MODE_MAX = CXL_MEM_MODE_EXMEM_ZONE,
};

static int cxl_mem_mode = CXL_MEM_MODE_SOFT_RESERVED;

enum CXL_GROUP_POLICY {
	CXL_GROUP_POLICY_ZONE = 0,
	CXL_GROUP_POLICY_NODE,
	CXL_GROUP_POLICY_NOOP,
	CXL_GROUP_POLICY_MAX = CXL_GROUP_POLICY_NOOP
};

static int cxl_group_policy = CXL_GROUP_POLICY_ZONE;

static struct resource *soft_reserved_res;
static int __cxl_register_one(struct resource *res, void *data);

/**
 * __remove_cxl_memory - Remove cxl.mem range from system memory
 * @nid: node id
 * @start: CXL.mem start address
 * @end: CXL.mem end address
 */
static int __remove_cxl_memory(int nid, u64 start, u64 end)
{
	int rc = 0;

	if (!start || !end) {
		pr_err("CXL: no cxl.mem range is specified\n");
		return -EINVAL;
	}

	pr_info("CXL: remove [mem %#010llx-%#010llx]\n", start, end);

	rc = offline_and_remove_memory(start, end - start + 1);
	if (rc) {
		pr_err("CXL: failed to offline and remove memory\n");
		return rc;
	}

	pr_info("CXL: remove subzone: nid: %d [mem %#010llx-%#010llx]\n",
			nid, start, end);
	rc = remove_subzone(nid, start, end);
	if (rc) {
		pr_err("CXL: failed to remove subzone\n");
		return rc;
	}

	return rc;
}

static int __remove_all_cxl_memory(void)
{
	int i;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
			continue;

		if (__remove_cxl_memory(cxl_meminfo.blk[i].nid,
					cxl_meminfo.blk[i].start, cxl_meminfo.blk[i].end - 1))
			return -1;
	}

	return 0;
}

/**
 * __add_cxl_memory - Add cxl.mem range to system memory
 * @nid: node id
 * @start: CXL.mem start address
 * @end: CXL.mem end address
 * @mode: cxl_mem_mode
 */
static int __add_cxl_memory(int nid, u64 start, u64 end, int mode)
{
	int mhp_value = MHP_MERGE_RESOURCE, rc = 0;

	if (!start || !end) {
		pr_err("CXL: no cxl.mem range is specified\n");
		return -EINVAL;
	}

	if (mode != CXL_MEM_MODE_EXMEM_ZONE && mode != CXL_MEM_MODE_NORMAL_ZONE)
		return -EINVAL;

	pr_info("CXL: add subzone: nid: %d [mem %#010llx-%#010llx]\n",
			nid, start, end);
	rc = add_subzone(nid, start, end);
	if (rc) {
		pr_err("CXL: failed to add subzone\n");
		return rc;
	}

	pr_info("CXL: add [mem %#010llx-%#010llx] as %s zone\n", start, end,
			mode == CXL_MEM_MODE_EXMEM_ZONE ? "ExMem" : "Normal");

	if (mode == CXL_MEM_MODE_EXMEM_ZONE)
		mhp_value |= MHP_EXMEM;

	rc = add_memory(nid, start, end - start + 1, mhp_value);
	if (rc)
		pr_err("CXL: failed to add memory\n");

	return rc;
}

static int __add_all_cxl_memory(int mode)
{
	int i;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
			continue;

		if (__add_cxl_memory(cxl_meminfo.blk[i].nid,
					cxl_meminfo.blk[i].start, cxl_meminfo.blk[i].end - 1,
					mode))
			return -1;
	}

	return 0;
}

/**
 * __rollback_soft_reserved_res - Rollback soft reserved resource
 */
static int __rollback_soft_reserved_res(void)
{
	int ret;

	if (!soft_reserved_res)
		return -1;

	pr_info("CXL: Rollback %s: [mem %#010llx-%#010llx]\n",
			soft_reserved_res->name, soft_reserved_res->start,
			soft_reserved_res->end);

	ret = insert_resource(&iomem_resource, soft_reserved_res);
	if (ret) {
		pr_err("CXL: failed to insert resource\n");
		return ret;
	}

	return 0;
}

/**
 * __change_cxl_mem_mode - Change CXL.mem mode
 * @old: old cxl mem mode
 * @new: new cxl mem mode
 */
static int __change_cxl_mem_mode(int old, int new)
{
	int ret = 0;

	if (old != cxl_mem_mode)
		return -1;

	if (old == new) {
		pr_info("CXL: same cxl mem mode\n");
		return 0;
	}

	if (old > CXL_MEM_MODE_MAX || new > CXL_MEM_MODE_MAX)
		return -1;

	if (old == CXL_MEM_MODE_SOFT_RESERVED) {
		ret = walk_iomem_res_desc(IORES_DESC_SOFT_RESERVED,
				IORESOURCE_MEM, 0, -1, &new, __cxl_register_one);
		if (ret)
			return -1;
	} else {
		/* First, remove CXL.mem region from system memory */
		if (__remove_all_cxl_memory() < 0)
			return -1;

		if (new == CXL_MEM_MODE_SOFT_RESERVED)
			ret = __rollback_soft_reserved_res();
		else
			ret = __add_all_cxl_memory(new);

		if (ret)
			return -1;
	}

	pr_info("CXL: cxl_mem_mode change from %d to %d\n", old, new);
	cxl_mem_mode = new;

	return 0;
}

static int __get_cxl_nid(enum CXL_GROUP_POLICY policy, u64 start)
{
	if (policy == CXL_GROUP_POLICY_ZONE)
		return 0;
	else if (policy == CXL_GROUP_POLICY_NODE) {
		int i;
		for (i = 0; i < cxl_meminfo.nr_blks; i++) {
			if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
				continue;

			return phys_to_target_node(cxl_meminfo.blk[i].start);
		}
	} else if (policy == CXL_GROUP_POLICY_NOOP)
		return phys_to_target_node(start);

	return NUMA_NO_NODE;
}

static int __change_cxl_group_policy(enum CXL_GROUP_POLICY old,
	   enum CXL_GROUP_POLICY new)
{
	int i, nid;

	if (old != cxl_group_policy)
		return -1;

	if (old == new) {
		pr_info("CXL: same cxl group policy\n");
		return 0;
	}

	if (old > CXL_GROUP_POLICY_MAX || new > CXL_GROUP_POLICY_MAX)
		return -1;

	if (__remove_all_cxl_memory() < 0)
		return -1;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
			continue;

		nid = __get_cxl_nid(new, cxl_meminfo.blk[i].start);
		if (nid < 0) {
			pr_err("CXL: no node info for %#010llx\n",
					cxl_meminfo.blk[i].start);
			return -1;
		}

		cxl_meminfo.blk[i].nid = nid;

		pr_info("CXL: nid: %d [mem %#010llx-%#010llx]\n",
				cxl_meminfo.blk[i].nid, cxl_meminfo.blk[i].start,
				cxl_meminfo.blk[i].end - 1);
	}

	if (__add_all_cxl_memory(cxl_mem_mode) < 0)
		return -1;

	pr_info("CXL: cxl_group_policy change from %d to %d\n", old, new);
	cxl_group_policy = new;

	return 0;
}

/**
 * cxl_mem_mode_show - Get cxl_mem_mode
 */
static ssize_t cxl_mem_mode_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", cxl_mem_mode);
}

/**
 * cxl_mem_mode_store - Set cxl_mem_mode
 */
static ssize_t cxl_mem_mode_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int ret, tmp;
	int old = cxl_mem_mode;

	ret = kstrtoint(buf, 10, &tmp);
	if (ret < 0) {
		pr_err("CXL: bad cxl mem mode\n");
		return count;
	}

	if (tmp > CXL_MEM_MODE_MAX) {
		pr_err("CXL: bad cxl mem mode\n");
		return count;
	}

	if (tmp == old)
		return count;

	if (tmp != CXL_MEM_MODE_EXMEM_ZONE) {
		pr_info("CXL: cxl_mem_mode is not ExMem zone, so change group policy to zone\n");
		if (__change_cxl_group_policy(cxl_group_policy, CXL_GROUP_POLICY_ZONE))
			return count;
	}

	if (__change_cxl_mem_mode(old, tmp) < 0) {
		pr_err("CXL: failed to change cxl mem mode\n");
		return count;
	}

	return count;
}

static struct kobj_attribute cxl_mem_mode_attribute =
	__ATTR(cxl_mem_mode, 0664, cxl_mem_mode_show, cxl_mem_mode_store);

/**
 * cxl_group_policy_show - Get cxl_group_policy
 */
static ssize_t cxl_group_policy_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	if (cxl_group_policy == CXL_GROUP_POLICY_ZONE)
		return sprintf(buf, "zone\n");
	else if (cxl_group_policy == CXL_GROUP_POLICY_NODE)
		return sprintf(buf, "node\n");
	return sprintf(buf, "noop\n");
}

/**
 * cxl_group_policy_store - Set cxl_group_policy
 */
static ssize_t cxl_group_policy_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	enum CXL_GROUP_POLICY tmp;

	if (cxl_mem_mode != CXL_MEM_MODE_EXMEM_ZONE) {
		pr_info("CXL: cxl_mem_mode is not 2.");
		return count;
	}

	if (strncmp(buf, "zone", 4) == 0)
		tmp = CXL_GROUP_POLICY_ZONE;
	else if (strncmp(buf, "node", 4) == 0)
		tmp = CXL_GROUP_POLICY_NODE;
	else if (strncmp(buf, "noop", 4) == 0)
		tmp = CXL_GROUP_POLICY_NOOP;
	else
		return -EINVAL;

	if (__change_cxl_group_policy(cxl_group_policy, tmp) < 0) {
		pr_err("CXL: failed to change cxl group policy\n");
		return count;
	}

	return count;
}

static struct kobj_attribute cxl_group_policy_attribute =
	__ATTR(cxl_group_policy, 0664, cxl_group_policy_show, cxl_group_policy_store);

static struct attribute *attrs[] = {
	&cxl_mem_mode_attribute.attr,
	&cxl_group_policy_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *cxl_kobj;
#endif /* CONFIG_EXMEM */

static int cxl_pci_mbox_wait_for_doorbell(struct cxl_dev_state *cxlds)
{
	const unsigned long start = jiffies;
	unsigned long end = start;

	while (cxl_doorbell_busy(cxlds)) {
		end = jiffies;

		if (time_after(end, start + CXL_MAILBOX_TIMEOUT_MS)) {
			/* Check again in case preempted before timeout test */
			if (!cxl_doorbell_busy(cxlds))
				break;
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	dev_dbg(cxlds->dev, "Doorbell wait took %dms",
		jiffies_to_msecs(end) - jiffies_to_msecs(start));
	return 0;
}

static void cxl_pci_mbox_timeout(struct cxl_dev_state *cxlds,
				 struct cxl_mbox_cmd *mbox_cmd)
{
	struct device *dev = cxlds->dev;

	dev_dbg(dev, "Mailbox command (opcode: %#x size: %zub) timed out\n",
		mbox_cmd->opcode, mbox_cmd->size_in);
}

/**
 * __cxl_pci_mbox_send_cmd() - Execute a mailbox command
 * @cxlds: The device state to communicate with.
 * @mbox_cmd: Command to send to the memory device.
 *
 * Context: Any context. Expects mbox_mutex to be held.
 * Return: -ETIMEDOUT if timeout occurred waiting for completion. 0 on success.
 *         Caller should check the return code in @mbox_cmd to make sure it
 *         succeeded.
 *
 * This is a generic form of the CXL mailbox send command thus only using the
 * registers defined by the mailbox capability ID - CXL 2.0 8.2.8.4. Memory
 * devices, and perhaps other types of CXL devices may have further information
 * available upon error conditions. Driver facilities wishing to send mailbox
 * commands should use the wrapper command.
 *
 * The CXL spec allows for up to two mailboxes. The intention is for the primary
 * mailbox to be OS controlled and the secondary mailbox to be used by system
 * firmware. This allows the OS and firmware to communicate with the device and
 * not need to coordinate with each other. The driver only uses the primary
 * mailbox.
 */
static int __cxl_pci_mbox_send_cmd(struct cxl_dev_state *cxlds,
				   struct cxl_mbox_cmd *mbox_cmd)
{
	void __iomem *payload = cxlds->regs.mbox + CXLDEV_MBOX_PAYLOAD_OFFSET;
	struct device *dev = cxlds->dev;
	u64 cmd_reg, status_reg;
	size_t out_len;
	int rc;

	lockdep_assert_held(&cxlds->mbox_mutex);

	/*
	 * Here are the steps from 8.2.8.4 of the CXL 2.0 spec.
	 *   1. Caller reads MB Control Register to verify doorbell is clear
	 *   2. Caller writes Command Register
	 *   3. Caller writes Command Payload Registers if input payload is non-empty
	 *   4. Caller writes MB Control Register to set doorbell
	 *   5. Caller either polls for doorbell to be clear or waits for interrupt if configured
	 *   6. Caller reads MB Status Register to fetch Return code
	 *   7. If command successful, Caller reads Command Register to get Payload Length
	 *   8. If output payload is non-empty, host reads Command Payload Registers
	 *
	 * Hardware is free to do whatever it wants before the doorbell is rung,
	 * and isn't allowed to change anything after it clears the doorbell. As
	 * such, steps 2 and 3 can happen in any order, and steps 6, 7, 8 can
	 * also happen in any order (though some orders might not make sense).
	 */

	/* #1 */
	if (cxl_doorbell_busy(cxlds)) {
		dev_err_ratelimited(dev, "Mailbox re-busy after acquiring\n");
		return -EBUSY;
	}

	cmd_reg = FIELD_PREP(CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK,
			     mbox_cmd->opcode);
	if (mbox_cmd->size_in) {
		if (WARN_ON(!mbox_cmd->payload_in))
			return -EINVAL;

		cmd_reg |= FIELD_PREP(CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK,
				      mbox_cmd->size_in);
		memcpy_toio(payload, mbox_cmd->payload_in, mbox_cmd->size_in);
	}

	/* #2, #3 */
	writeq(cmd_reg, cxlds->regs.mbox + CXLDEV_MBOX_CMD_OFFSET);

	/* #4 */
	dev_dbg(dev, "Sending command\n");
	writel(CXLDEV_MBOX_CTRL_DOORBELL,
	       cxlds->regs.mbox + CXLDEV_MBOX_CTRL_OFFSET);

	/* #5 */
	rc = cxl_pci_mbox_wait_for_doorbell(cxlds);
	if (rc == -ETIMEDOUT) {
		cxl_pci_mbox_timeout(cxlds, mbox_cmd);
		return rc;
	}

	/* #6 */
	status_reg = readq(cxlds->regs.mbox + CXLDEV_MBOX_STATUS_OFFSET);
	mbox_cmd->return_code =
		FIELD_GET(CXLDEV_MBOX_STATUS_RET_CODE_MASK, status_reg);

	if (mbox_cmd->return_code != 0) {
		dev_dbg(dev, "Mailbox operation had an error\n");
		return 0;
	}

	/* #7 */
	cmd_reg = readq(cxlds->regs.mbox + CXLDEV_MBOX_CMD_OFFSET);
	out_len = FIELD_GET(CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK, cmd_reg);

	/* #8 */
	if (out_len && mbox_cmd->payload_out) {
		/*
		 * Sanitize the copy. If hardware misbehaves, out_len per the
		 * spec can actually be greater than the max allowed size (21
		 * bits available but spec defined 1M max). The caller also may
		 * have requested less data than the hardware supplied even
		 * within spec.
		 */
		size_t n = min3(mbox_cmd->size_out, cxlds->payload_size, out_len);

		memcpy_fromio(mbox_cmd->payload_out, payload, n);
		mbox_cmd->size_out = n;
	} else {
		mbox_cmd->size_out = 0;
	}

	return 0;
}

/**
 * cxl_pci_mbox_get() - Acquire exclusive access to the mailbox.
 * @cxlds: The device state to gain access to.
 *
 * Context: Any context. Takes the mbox_mutex.
 * Return: 0 if exclusive access was acquired.
 */
static int cxl_pci_mbox_get(struct cxl_dev_state *cxlds)
{
	struct device *dev = cxlds->dev;
	u64 md_status;
	int rc;

	mutex_lock_io(&cxlds->mbox_mutex);

	/*
	 * XXX: There is some amount of ambiguity in the 2.0 version of the spec
	 * around the mailbox interface ready (8.2.8.5.1.1).  The purpose of the
	 * bit is to allow firmware running on the device to notify the driver
	 * that it's ready to receive commands. It is unclear if the bit needs
	 * to be read for each transaction mailbox, ie. the firmware can switch
	 * it on and off as needed. Second, there is no defined timeout for
	 * mailbox ready, like there is for the doorbell interface.
	 *
	 * Assumptions:
	 * 1. The firmware might toggle the Mailbox Interface Ready bit, check
	 *    it for every command.
	 *
	 * 2. If the doorbell is clear, the firmware should have first set the
	 *    Mailbox Interface Ready bit. Therefore, waiting for the doorbell
	 *    to be ready is sufficient.
	 */
	rc = cxl_pci_mbox_wait_for_doorbell(cxlds);
	if (rc) {
		dev_warn(dev, "Mailbox interface not ready\n");
		goto out;
	}

	md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
	if (!(md_status & CXLMDEV_MBOX_IF_READY && CXLMDEV_READY(md_status))) {
		dev_err(dev, "mbox: reported doorbell ready, but not mbox ready\n");
		rc = -EBUSY;
		goto out;
	}

	/*
	 * Hardware shouldn't allow a ready status but also have failure bits
	 * set. Spit out an error, this should be a bug report
	 */
	rc = -EFAULT;
	if (md_status & CXLMDEV_DEV_FATAL) {
		dev_err(dev, "mbox: reported ready, but fatal\n");
		goto out;
	}
	if (md_status & CXLMDEV_FW_HALT) {
		dev_err(dev, "mbox: reported ready, but halted\n");
		goto out;
	}
	if (CXLMDEV_RESET_NEEDED(md_status)) {
		dev_err(dev, "mbox: reported ready, but reset needed\n");
		goto out;
	}

	/* with lock held */
	return 0;

out:
	mutex_unlock(&cxlds->mbox_mutex);
	return rc;
}

/**
 * cxl_pci_mbox_put() - Release exclusive access to the mailbox.
 * @cxlds: The device state to communicate with.
 *
 * Context: Any context. Expects mbox_mutex to be held.
 */
static void cxl_pci_mbox_put(struct cxl_dev_state *cxlds)
{
	mutex_unlock(&cxlds->mbox_mutex);
}

static int cxl_pci_mbox_send(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	int rc;

	rc = cxl_pci_mbox_get(cxlds);
	if (rc)
		return rc;

	rc = __cxl_pci_mbox_send_cmd(cxlds, cmd);
	cxl_pci_mbox_put(cxlds);

	return rc;
}

static int cxl_pci_setup_mailbox(struct cxl_dev_state *cxlds)
{
	const int cap = readl(cxlds->regs.mbox + CXLDEV_MBOX_CAPS_OFFSET);

	cxlds->mbox_send = cxl_pci_mbox_send;
	cxlds->payload_size =
		1 << FIELD_GET(CXLDEV_MBOX_CAP_PAYLOAD_SIZE_MASK, cap);

	/*
	 * CXL 2.0 8.2.8.4.3 Mailbox Capabilities Register
	 *
	 * If the size is too small, mandatory commands will not work and so
	 * there's no point in going forward. If the size is too large, there's
	 * no harm is soft limiting it.
	 */
	cxlds->payload_size = min_t(size_t, cxlds->payload_size, SZ_1M);
	if (cxlds->payload_size < 256) {
		dev_err(cxlds->dev, "Mailbox is too small (%zub)",
			cxlds->payload_size);
		return -ENXIO;
	}

	dev_dbg(cxlds->dev, "Mailbox payload sized %zu",
		cxlds->payload_size);

	return 0;
}

static int cxl_map_regblock(struct pci_dev *pdev, struct cxl_register_map *map)
{
	void __iomem *addr;
	int bar = map->barno;
	struct device *dev = &pdev->dev;
	resource_size_t offset = map->block_offset;

	/* Basic sanity check that BAR is big enough */
	if (pci_resource_len(pdev, bar) < offset) {
		dev_err(dev, "BAR%d: %pr: too small (offset: %pa)\n", bar,
			&pdev->resource[bar], &offset);
		return -ENXIO;
	}

	addr = pci_iomap(pdev, bar, 0);
	if (!addr) {
		dev_err(dev, "failed to map registers\n");
		return -ENOMEM;
	}

	dev_dbg(dev, "Mapped CXL Memory Device resource bar %u @ %pa\n",
		bar, &offset);

	map->base = addr + map->block_offset;
	return 0;
}

static void cxl_unmap_regblock(struct pci_dev *pdev,
			       struct cxl_register_map *map)
{
	pci_iounmap(pdev, map->base - map->block_offset);
	map->base = NULL;
}

static int cxl_probe_regs(struct pci_dev *pdev, struct cxl_register_map *map)
{
	struct cxl_component_reg_map *comp_map;
	struct cxl_device_reg_map *dev_map;
	struct device *dev = &pdev->dev;
	void __iomem *base = map->base;

	switch (map->reg_type) {
	case CXL_REGLOC_RBI_COMPONENT:
		comp_map = &map->component_map;
		cxl_probe_component_regs(dev, base, comp_map);
		if (!comp_map->hdm_decoder.valid) {
			dev_err(dev, "HDM decoder registers not found\n");
			return -ENXIO;
		}

		dev_dbg(dev, "Set up component registers\n");
		break;
	case CXL_REGLOC_RBI_MEMDEV:
		dev_map = &map->device_map;
		cxl_probe_device_regs(dev, base, dev_map);
		if (!dev_map->status.valid || !dev_map->mbox.valid ||
		    !dev_map->memdev.valid) {
			dev_err(dev, "registers not found: %s%s%s\n",
				!dev_map->status.valid ? "status " : "",
				!dev_map->mbox.valid ? "mbox " : "",
				!dev_map->memdev.valid ? "memdev " : "");
			return -ENXIO;
		}

		dev_dbg(dev, "Probing device registers...\n");
		break;
	default:
		break;
	}

	return 0;
}

static int cxl_map_regs(struct cxl_dev_state *cxlds, struct cxl_register_map *map)
{
	struct device *dev = cxlds->dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	switch (map->reg_type) {
	case CXL_REGLOC_RBI_COMPONENT:
		cxl_map_component_regs(pdev, &cxlds->regs.component, map);
		dev_dbg(dev, "Mapping component registers...\n");
		break;
	case CXL_REGLOC_RBI_MEMDEV:
		cxl_map_device_regs(pdev, &cxlds->regs.device_regs, map);
		dev_dbg(dev, "Probing device registers...\n");
		break;
	default:
		break;
	}

	return 0;
}

static void cxl_decode_regblock(u32 reg_lo, u32 reg_hi,
				struct cxl_register_map *map)
{
	map->block_offset =
		((u64)reg_hi << 32) | (reg_lo & CXL_REGLOC_ADDR_MASK);
	map->barno = FIELD_GET(CXL_REGLOC_BIR_MASK, reg_lo);
	map->reg_type = FIELD_GET(CXL_REGLOC_RBI_MASK, reg_lo);
}

/**
 * cxl_find_regblock() - Locate register blocks by type
 * @pdev: The CXL PCI device to enumerate.
 * @type: Register Block Indicator id
 * @map: Enumeration output, clobbered on error
 *
 * Return: 0 if register block enumerated, negative error code otherwise
 *
 * A CXL DVSEC may point to one or more register blocks, search for them
 * by @type.
 */
static int cxl_find_regblock(struct pci_dev *pdev, enum cxl_regloc_type type,
			     struct cxl_register_map *map)
{
	u32 regloc_size, regblocks;
	int regloc, i;

	regloc = pci_find_dvsec_capability(pdev, PCI_DVSEC_VENDOR_ID_CXL,
					   PCI_DVSEC_ID_CXL_REGLOC_DVSEC_ID);
	if (!regloc)
		return -ENXIO;

	pci_read_config_dword(pdev, regloc + PCI_DVSEC_HEADER1, &regloc_size);
	regloc_size = FIELD_GET(PCI_DVSEC_HEADER1_LENGTH_MASK, regloc_size);

	regloc += PCI_DVSEC_ID_CXL_REGLOC_BLOCK1_OFFSET;
	regblocks = (regloc_size - PCI_DVSEC_ID_CXL_REGLOC_BLOCK1_OFFSET) / 8;

	for (i = 0; i < regblocks; i++, regloc += 8) {
		u32 reg_lo, reg_hi;

		pci_read_config_dword(pdev, regloc, &reg_lo);
		pci_read_config_dword(pdev, regloc + 4, &reg_hi);

		cxl_decode_regblock(reg_lo, reg_hi, map);

		if (map->reg_type == type)
			return 0;
	}

	return -ENODEV;
}

static int cxl_setup_regs(struct pci_dev *pdev, enum cxl_regloc_type type,
			  struct cxl_register_map *map)
{
	int rc;

	rc = cxl_find_regblock(pdev, type, map);
	if (rc)
		return rc;

	rc = cxl_map_regblock(pdev, map);
	if (rc)
		return rc;

	rc = cxl_probe_regs(pdev, map);
	cxl_unmap_regblock(pdev, map);

	return rc;
}

#ifdef CONFIG_EXMEM
/**
 * __cxl_register_one - Change soft reserved memory region to usable
 * @res: soft reserved memory region
 * @data: cxl mem mode
 */
static int __cxl_register_one(struct resource *res, void *data)
{
	int node;
	struct resource **p, *target = NULL;
	int mode, i;

	/*
	 * If the resource is not a top-level resource it was already
	 * assigned to a device by the HMAT parsing.
	 */
	if (res->parent != &iomem_resource) {
		pr_info("CXL: skip %pr, already claimed\n", res);
		return 0;
	}

	if (!data)
		return -1;

	mode = (int)(*(int *)data);

	if (mode < 0 || mode > CXL_MEM_MODE_MAX)
		return -1;

	pr_info("CXL: soft reserved: [mem %#010llx-%#010llx]\n",
			res->start, res->end);

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
			continue;

		node = __get_cxl_nid(cxl_group_policy, cxl_meminfo.blk[i].start);
		if (node < 0) {
			pr_err("CXL: no node info for %#010llx\n",
					cxl_meminfo.blk[i].start);
			return -1;
		}

		cxl_meminfo.blk[i].nid = node;

		pr_info("CXL: nid: %d [mem %#010llx-%#010llx]\n",
				node, cxl_meminfo.blk[i].start, cxl_meminfo.blk[i].end - 1);
	}

	p = &res->parent->child;
	for (;;) {
		struct resource *tmp = *p;
		if (!tmp)
			break;
		if (tmp->start == res->start && tmp->end == res->end) {
			target = tmp;
			break;
		}
		p = &tmp->sibling;
	}
	if (target) {
		soft_reserved_res = target;
		if (release_resource(target)) {
			pr_err("CXL: failed to release resource\n");
			return -1;
		}
	}

	if (__add_all_cxl_memory(mode))
		return -1;

	return 0;
}
#endif /* CONFIG_EXMEM */

static int cxl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct cxl_register_map map;
	struct cxl_memdev *cxlmd;
	struct cxl_dev_state *cxlds;
	int rc;

	/*
	 * Double check the anonymous union trickery in struct cxl_regs
	 * FIXME switch to struct_group()
	 */
	BUILD_BUG_ON(offsetof(struct cxl_regs, memdev) !=
		     offsetof(struct cxl_regs, device_regs.memdev));

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	cxlds = cxl_dev_state_create(&pdev->dev);
	if (IS_ERR(cxlds))
		return PTR_ERR(cxlds);

	rc = cxl_setup_regs(pdev, CXL_REGLOC_RBI_MEMDEV, &map);
	if (rc)
		return rc;

	rc = cxl_map_regs(cxlds, &map);
	if (rc)
		return rc;

	rc = cxl_pci_setup_mailbox(cxlds);
	if (rc)
		return rc;

	rc = cxl_enumerate_cmds(cxlds);
	if (rc)
		return rc;

	rc = cxl_dev_state_identify(cxlds);
	if (rc)
		return rc;

	rc = cxl_mem_create_range_info(cxlds);
	if (rc)
		return rc;

	cxlmd = devm_cxl_add_memdev(cxlds);
	if (IS_ERR(cxlmd))
		return PTR_ERR(cxlmd);

	if (range_len(&cxlds->pmem_range) && IS_ENABLED(CONFIG_CXL_PMEM))
		rc = devm_cxl_add_nvdimm(&pdev->dev, cxlmd);

	return rc;
}

static const struct pci_device_id cxl_mem_pci_tbl[] = {
	/* PCI class code for CXL.mem Type-3 Devices */
	{ PCI_DEVICE_CLASS((PCI_CLASS_MEMORY_CXL << 8 | CXL_MEMORY_PROGIF), ~0)},
	{ /* terminate list */ },
};
MODULE_DEVICE_TABLE(pci, cxl_mem_pci_tbl);

static struct pci_driver cxl_pci_driver = {
	.name			= KBUILD_MODNAME,
	.id_table		= cxl_mem_pci_tbl,
	.probe			= cxl_pci_probe,
	.driver	= {
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
	},
};

#ifdef CONFIG_EXMEM
static __init int exmem_cxl_pci_init(void)
{
	int rc;

	rc = pci_register_driver(&cxl_pci_driver);
	if (rc)
		return rc;

	numa_get_reserved_meminfo(&cxl_meminfo);
	check_reserved_meminfo(&cxl_meminfo);
	print_numa_reserved_meminfo(&cxl_meminfo);

	cxl_kobj = kobject_create_and_add("cxl", kernel_kobj);
	if (!cxl_kobj)
		return 0;

	/* Create the files associated with this kobject */
	rc = sysfs_create_group(cxl_kobj, &attr_group);
	if (rc) {
		kobject_put(cxl_kobj);
		return 0;
	}

	/* Activate soft reserved region as a CXL zone at init time */
	if (__change_cxl_mem_mode(cxl_mem_mode, CXL_MEM_MODE_EXMEM_ZONE) < 0) {
		kobject_put(cxl_kobj);
		return 0;
	}

	return 0;
}

static __exit void exmem_cxl_pci_exit(void)
{
	__change_cxl_mem_mode(cxl_mem_mode, CXL_MEM_MODE_SOFT_RESERVED);
	kobject_put(cxl_kobj);
	pci_unregister_driver(&cxl_pci_driver);
}
#endif

MODULE_LICENSE("GPL v2");
#ifdef CONFIG_EXMEM
module_init(exmem_cxl_pci_init);
module_exit(exmem_cxl_pci_exit);
#else
module_pci_driver(cxl_pci_driver);
#endif
MODULE_IMPORT_NS(CXL);
