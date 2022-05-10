// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sizes.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/io.h>
#include "cxlmem.h"
#include "cxlpci.h"
#include "cxl.h"
#ifdef CONFIG_EXMEM
#include <linux/numa.h>

enum CXL_MEM_MODE {
	CXL_MEM_MODE_SOFT_RESERVED = 0,
	CXL_MEM_MODE_ZONE_NORMAL,
	CXL_MEM_MODE_ZONE_EXMEM,
	CXL_MEM_MODE_MAX = CXL_MEM_MODE_ZONE_EXMEM,
};

enum CXL_GROUP_POLICY {
	CXL_GROUP_POLICY_ZONE = 0,
	CXL_GROUP_POLICY_NODE,
	CXL_GROUP_POLICY_NOOP,
	CXL_GROUP_POLICY_MAX = CXL_GROUP_POLICY_NOOP
};

static enum CXL_MEM_MODE cxl_mem_mode = CXL_MEM_MODE_SOFT_RESERVED;
static enum CXL_GROUP_POLICY cxl_group_policy = CXL_GROUP_POLICY_ZONE;

struct cxl_memblk {
	u64 start;
	u64 end;
	int dev_numa_node;
	int nid;
	struct resource *res;
};

struct cxl_meminfo {
	int nr_blks;
	int dev_numa_node_to_nid_map[MAX_NUMNODES];
	struct cxl_memblk blk[MAX_NUMNODES];
};

static bool dvsec_only_device = false;
static struct cxl_meminfo cxl_meminfo;
static DEFINE_MUTEX(cxl_meminfo_lock);

/* Memory resource name used for add_memory_driver_managed(). */
static const char *smdk_res_name = "System RAM (cxl)";

static int __get_cxl_nid(enum CXL_GROUP_POLICY policy, struct cxl_memblk *cmb)
{
	int nid;

	if (!cmb)
		return NUMA_NO_NODE;

	switch (policy) {
	case CXL_GROUP_POLICY_ZONE:
		return cmb->dev_numa_node;
	case CXL_GROUP_POLICY_NODE:
		nid = cxl_meminfo.dev_numa_node_to_nid_map[cmb->dev_numa_node];
		if (nid != NUMA_NO_NODE)
			return nid;

		for (nid = 0; nid < MAX_NUMNODES; nid++) {
			if (node_online(nid))
				continue;

			cxl_meminfo.dev_numa_node_to_nid_map[cmb->dev_numa_node] = nid;
			return nid;
		}

		return NUMA_NO_NODE;
	case CXL_GROUP_POLICY_NOOP:
		nid = phys_to_target_node(cmb->start);
		return nid == NUMA_NO_NODE ? 0 : nid;
	}

	return NUMA_NO_NODE;
}

/**
 * __add_cxl_memory - Add cxl.mem range to system memory
 * @cmb: information descriptor for cxl memory block
 * @mode: cxl_mem_mode
 */
static int __add_cxl_memory(struct cxl_memblk *cmb, enum CXL_MEM_MODE mode)
{
	int rc = 0;
	int mhp_value = MHP_MERGE_RESOURCE;
	struct resource *res;
	u64 size;

	if (!cmb) {
		pr_err("CXL: cxl memblock is null\n");
		return -EINVAL;
	}

	if (cmb->start == 0 || cmb->end == 0) {
		pr_err("CXL: no cxl.mem range is specified\n");
		return -EINVAL;
	}

	if (mode != CXL_MEM_MODE_ZONE_EXMEM && mode != CXL_MEM_MODE_ZONE_NORMAL)
		return -EINVAL;

	pr_info("CXL: request mem region: [mem %#010llx-%#010llx]\n", cmb->start,
			cmb->end);

	size = cmb->end - cmb->start + 1;
	res = request_mem_region(cmb->start, size, "cxl");
	if (!res) {
		pr_err("CXL: failed to request mem region\n");
		return -EBUSY;
	}

	res->flags = IORESOURCE_SYSTEM_RAM;
	cmb->res = res;

	pr_info("CXL: add subzone: nid: %d [mem %#010llx-%#010llx]\n", cmb->nid,
			cmb->start, cmb->end);

	rc = add_subzone(cmb->nid, cmb->start, cmb->end);
	if (rc) {
		pr_err("CXL: failed to add subzone\n");
		goto err_add_subzone;
	}

	pr_info("CXL: add [mem %#010llx-%#010llx] as %s zone\n", cmb->start,
			cmb->end, mode == CXL_MEM_MODE_ZONE_EXMEM ? "ExMem" : "Normal");

	if (mode == CXL_MEM_MODE_ZONE_EXMEM)
		mhp_value |= MHP_EXMEM;

	rc = add_memory_driver_managed(cmb->nid, cmb->start, size, smdk_res_name,
			mhp_value);
	if (rc) {
		pr_err("CXL: failed to add memory with %d\n", rc);
		goto err_add_memory;
	}

	return 0;

err_add_memory:
	if (remove_subzone(cmb->nid, cmb->start, cmb->end))
		pr_err("CXL: failed to remove subzone\n");

err_add_subzone:
	release_resource(res);
	kfree(res);
	cmb->res = NULL;
	return rc;
}

/**
 * __remove_cxl_memory - Remove cxl.mem range from system memory
 * @cmb: information descriptor for cxl memory block
 */
static int __remove_cxl_memory(struct cxl_memblk *cmb)
{
	int rc;
	u64 size;

	if (!cmb) {
		pr_err("CXL: cxl memblock is null\n");
		return -EINVAL;
	}

	if (cmb->start == 0 || cmb->end == 0) {
		pr_err("CXL: no cxl.mem range is specified\n");
		return -EINVAL;
	}

	pr_info("CXL: remove [mem %#010llx-%#010llx]\n", cmb->start, cmb->end);

	size = cmb->end - cmb->start + 1;
	rc = offline_and_remove_memory(cmb->start, size);
	if (rc) {
		pr_err("CXL: failed to offline and remove memory\n");
		return rc;
	}

	pr_info("CXL: remove subzone: nid: %d [mem %#010llx-%#010llx]\n",
			cmb->nid, cmb->start, cmb->end);

	rc = remove_subzone(cmb->nid, cmb->start, cmb->end);
	if (rc) {
		pr_err("CXL: failed to remove subzone\n");
		return rc;
	}

	pr_info("CXL: release resource %pr\n", cmb->res);

	release_resource(cmb->res);
	kfree(cmb->res);
	cmb->res = NULL;

	return rc;
}

static void print_cxl_meminfo(void)
{
	int i, cnt = 0;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
			continue;

		pr_info("CXL: nid: %d, dev nid: %d, [mem %#010llx-%#010llx]\n",
				cxl_meminfo.blk[i].nid, cxl_meminfo.blk[i].dev_numa_node,
				cxl_meminfo.blk[i].start, cxl_meminfo.blk[i].end);

		cnt++;
	}

	pr_info("CXL: node count: %d\n", cnt);
}

static struct cxl_memblk *find_cxl_meminfo(u64 start, u64 end)
{
	int i;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].start == start && cxl_meminfo.blk[i].end == end)
			return &cxl_meminfo.blk[i];
	}

	return NULL;
}

static int __check_cxl_range(struct resource *res, void *data)
{
	int i = 0;

	pr_info("CXL: soft reserved: [mem %#010llx-%#010llx]\n",
			res->start, res->end);

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (res->start <= cxl_meminfo.blk[i].start &&
				cxl_meminfo.blk[i].end <= res->end)
			continue;

		pr_info("CXL: out of range: [mem %#010llx-%#010llx]\n",
				cxl_meminfo.blk[i].start, cxl_meminfo.blk[i].end);

		cxl_meminfo.blk[i].nid = NUMA_NO_NODE;
	}

	return 0;
}

static void check_reserved_meminfo(void)
{
	mutex_lock(&cxl_meminfo_lock);
	walk_iomem_res_desc(IORES_DESC_SOFT_RESERVED,
			IORESOURCE_MEM, 0, -1, NULL, __check_cxl_range);
	mutex_unlock(&cxl_meminfo_lock);
}

static struct cxl_memblk *add_cxl_meminfo(u64 start, u64 size,
		int dev_numa_node, int nid)
{
	struct cxl_memblk *cmb = NULL;

	if (nid < 0 || cxl_meminfo.nr_blks >= MAX_NUMNODES)
		return NULL;

	cmb = &cxl_meminfo.blk[cxl_meminfo.nr_blks++];
	cmb->start = start;
	cmb->end = start + size - 1;
	if (dev_numa_node == NUMA_NO_NODE)
		cmb->dev_numa_node = 0;
	else
		cmb->dev_numa_node = dev_numa_node;
	cmb->nid = nid;

	pr_info("CXL: add meminfo: nid: %d, [mem %#010llx-%#010llx]\n",
			nid, cmb->start, cmb->end);

	return cmb;
}

static int get_cxl_meminfo(void)
{
	int nr_blks = numa_get_reserved_meminfo_cnt();
	int nid;
	u64 start, end;
	int rc, i;
	struct cxl_memblk *cmb;

	for (i = 0; i < MAX_NUMNODES; i++)
		cxl_meminfo.dev_numa_node_to_nid_map[i] = NUMA_NO_NODE;

	for (i = 0; i < nr_blks; i++) {
		rc = numa_get_reserved_meminfo(i, &nid, &start, &end);
		if (rc) {
			pr_err("CXL: Failed to get cxl meminfo: (%d)\n", rc);
			return -1;
		}

		mutex_lock(&cxl_meminfo_lock);
		cmb = find_cxl_meminfo(start, end);
		if (cmb) {
			pr_warn("CXL: same info, [mem %#010llx-%#010llx]\n", start, end);
			mutex_unlock(&cxl_meminfo_lock);
			continue;
		}

		if (!add_cxl_meminfo(start, end - start, NUMA_NO_NODE, nid)) {
			pr_err("CXL: failed to add cxl meminfo\n");
			mutex_unlock(&cxl_meminfo_lock);
			return -1;
		}

		mutex_unlock(&cxl_meminfo_lock);
	}

	check_reserved_meminfo();
	print_cxl_meminfo();

	return 0;
}

static int update_or_add_cxl_meminfo(u64 start, u64 end, int dev_numa_node)
{
	int nid;
	int rc = 0;
	struct cxl_memblk *cmb;
	u64 size = end - start + 1;

	if (start == 0 || end == 0 || size == 0)
		return -EINVAL;

	mutex_lock(&cxl_meminfo_lock);

	cmb = find_cxl_meminfo(start, end);
	if (cmb) {
		if (dev_numa_node != NUMA_NO_NODE) {
			pr_info("CXL: update dev_numa_node to %d [mem %#010llx-%#010llx]\n",
					dev_numa_node, cmb->start, cmb->end);
			cmb->dev_numa_node = dev_numa_node;

			if (cxl_mem_mode == CXL_MEM_MODE_ZONE_EXMEM) {
				nid = __get_cxl_nid(cxl_group_policy, cmb);
				if (nid != NUMA_NO_NODE && cmb->nid != nid) {
					rc = __remove_cxl_memory(cmb);
					if (rc)
						goto out;

					cmb->nid = nid;
					rc = __add_cxl_memory(cmb, cxl_mem_mode);
					if (rc)
						goto out;
				}
			}
		}
		mutex_unlock(&cxl_meminfo_lock);
		return 0;
	}

	nid = phys_to_target_node(start);
	if (nid == NUMA_NO_NODE) {
		pr_warn("CXL: Warning: there's no node info. set nid 0.\n");
		nid = 0;
	}

	cmb = add_cxl_meminfo(start, size, dev_numa_node, nid);
	if (!cmb) {
		mutex_unlock(&cxl_meminfo_lock);
		pr_err("CXL: Failed to add cxl meminfo\n");
		return -1;
	}

	nid = __get_cxl_nid(cxl_group_policy, cmb);
	if (nid == NUMA_NO_NODE) {
		mutex_unlock(&cxl_meminfo_lock);
		pr_err("CXL: no node information found [mem %#010llx-%#010llx]\n",
				start, end);
		return -1;
	}

	cmb->nid = nid;

	if (cxl_mem_mode != CXL_MEM_MODE_SOFT_RESERVED) {
		rc = __add_cxl_memory(cmb, cxl_mem_mode);
		if (rc)
			goto out;
	}

out:
	mutex_unlock(&cxl_meminfo_lock);
	return rc;
}

static int register_cxl_dvsec_ranges(struct cxl_dev_state *cxlds)
{
	struct cxl_endpoint_dvsec_info *info = &cxlds->info;
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	struct device *dev = &pdev->dev;
	int i, rc;

	pr_info("CXL: register cxl dvsec ranges\n");

	for (i = 0; i < info->ranges; i++) {
		if (info->dvsec_range[i].start == 0 ||
				info->dvsec_range[i].end == 0)
			continue;

		if (phys_to_target_node(info->dvsec_range[i].start) == NUMA_NO_NODE)
			dvsec_only_device = true;

		rc = update_or_add_cxl_meminfo(info->dvsec_range[i].start,
				info->dvsec_range[i].end, dev->numa_node);
		if (!rc)
			return rc;
	}

	print_cxl_meminfo();
	return 0;
}

int register_cxl_cfmws_ranges(struct cxl_decoder *cxld)
{
	struct device *dev = &cxld->dev;
	int rc;

	pr_info("CXL: register cxl cfmws ranges\n");

	rc = update_or_add_cxl_meminfo(cxld->platform_res.start,
			cxld->platform_res.end, dev->numa_node);
	if (rc)
		return rc;

	print_cxl_meminfo();
	return 0;
}
EXPORT_SYMBOL_NS_GPL(register_cxl_cfmws_ranges, CXL);

static int __rollback_memory_block(int fail_idx, enum CXL_MEM_MODE mode)
{
	int i;

	for (i = 0; i < fail_idx; i++) {
		if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
			continue;

		if (__add_cxl_memory(&cxl_meminfo.blk[i], mode))
			return -1;
	}

	return 0;
}

static int __add_all_cxl_memory(enum CXL_MEM_MODE mode,
		enum CXL_GROUP_POLICY policy)
{
	int i, nid;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
			continue;

		nid = __get_cxl_nid(policy, &cxl_meminfo.blk[i]);
		if (nid == NUMA_NO_NODE) {
			pr_err("CXL: no node information found [mem %#010llx-%#010llx]\n",
					cxl_meminfo.blk[i].start, cxl_meminfo.blk[i].end);
			return -1;
		}

		cxl_meminfo.blk[i].nid = nid;

		if (__add_cxl_memory(&cxl_meminfo.blk[i], mode))
			return -1;
	}

	return 0;
}

static int __remove_all_cxl_memory(int *fail_idx)
{
	int i;

	if (!fail_idx)
		return -EINVAL;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].nid == NUMA_NO_NODE)
			continue;

		if (__remove_cxl_memory(&cxl_meminfo.blk[i])) {
			*fail_idx = i;
			return -1;
		}
	}

	return 0;
}

/**
 * __change_cxl_mem_mode - Change CXL.mem mode
 * @old: old cxl_mem_mode
 * @new: new cxl_mem_mode
 */
static int __change_cxl_mem_mode(enum CXL_MEM_MODE old, enum CXL_MEM_MODE new)
{
	int fail_idx = 0;

	if (old > CXL_MEM_MODE_MAX || new > CXL_MEM_MODE_MAX)
		return -EINVAL;

	if (old != cxl_mem_mode)
		return -EINVAL;

	if (old == new) {
		pr_info("CXL: same cxl_mem_mode, do nothing\n");
		return 0;
	}

	mutex_lock(&cxl_meminfo_lock);
	if (old != CXL_MEM_MODE_SOFT_RESERVED) {
		if (__remove_all_cxl_memory(&fail_idx) < 0) {
			pr_info("CXL: Failed to remove cxl memory, try to rollback\n");
			if (__rollback_memory_block(fail_idx, cxl_mem_mode)) {
				mutex_unlock(&cxl_meminfo_lock);
				return -1;
			}

			mutex_unlock(&cxl_meminfo_lock);
			return -1;
		}
	}

	if (new != CXL_MEM_MODE_SOFT_RESERVED) {
		if (__add_all_cxl_memory(new, cxl_group_policy) < 0) {
			mutex_unlock(&cxl_meminfo_lock);
			return -1;
		}
	}
	mutex_unlock(&cxl_meminfo_lock);

	pr_info("CXL: cxl_mem_mode is changed from %d to %d\n", old, new);
	cxl_mem_mode = new;

	return 0;
}

/**
 * __change_cxl_group_policy - Change memory parititon
 * @old: old cxl_group_policy
 * @new: new cxl_group_policy
 */
static int __change_cxl_group_policy(enum CXL_GROUP_POLICY old,
	   enum CXL_GROUP_POLICY new)
{
	int i, fail_idx = 0;

	if (old > CXL_GROUP_POLICY_MAX || new > CXL_GROUP_POLICY_MAX)
		return -EINVAL;

	if (old != cxl_group_policy)
		return -EINVAL;

	if (old == new) {
		pr_info("CXL: same cxl_group_policy, do nothing\n");
		return 0;
	}

	if (dvsec_only_device) {
		pr_warn("CXL: Warning: There's dvsec only device, do nothing\n");
		return 0;
	}

	mutex_lock(&cxl_meminfo_lock);
	if (new == CXL_GROUP_POLICY_NODE) {
		for (i = 0; i < MAX_NUMNODES; i++)
			cxl_meminfo.dev_numa_node_to_nid_map[i] = NUMA_NO_NODE;
	}

	if (__remove_all_cxl_memory(&fail_idx) < 0) {
		pr_info("CXL: Failed to remove cxl memory, try to rollback\n");
		if (__rollback_memory_block(fail_idx, cxl_mem_mode)) {
			mutex_unlock(&cxl_meminfo_lock);
			return -1;
		}

		mutex_unlock(&cxl_meminfo_lock);
		return -1;
	}

	if (__add_all_cxl_memory(cxl_mem_mode, new) < 0) {
		mutex_unlock(&cxl_meminfo_lock);
		return -1;
	}
	mutex_unlock(&cxl_meminfo_lock);

	pr_info("CXL: cxl_group_policy is changed from %d to %d\n", old, new);
	cxl_group_policy = new;

	return 0;
}
#endif /* CONFIG_EXMEM */

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

/*
 * CXL 2.0 ECN "Add Mailbox Ready Time" defines a capability field to
 * dictate how long to wait for the mailbox to become ready. The new
 * field allows the device to tell software the amount of time to wait
 * before mailbox ready. This field per the spec theoretically allows
 * for up to 255 seconds. 255 seconds is unreasonably long, its longer
 * than the maximum SATA port link recovery wait. Default to 60 seconds
 * until someone builds a CXL device that needs more time in practice.
 */
static unsigned short mbox_ready_timeout = 60;
module_param(mbox_ready_timeout, ushort, 0644);
MODULE_PARM_DESC(mbox_ready_timeout,
		 "seconds to wait for mailbox ready / memory active status");

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

#define cxl_err(dev, status, msg)                                        \
	dev_err_ratelimited(dev, msg ", device state %s%s\n",                  \
			    status & CXLMDEV_DEV_FATAL ? " fatal" : "",        \
			    status & CXLMDEV_FW_HALT ? " firmware-halt" : "")

#define cxl_cmd_err(dev, cmd, status, msg)                               \
	dev_err_ratelimited(dev, msg " (opcode: %#x), device state %s%s\n",    \
			    (cmd)->opcode,                                     \
			    status & CXLMDEV_DEV_FATAL ? " fatal" : "",        \
			    status & CXLMDEV_FW_HALT ? " firmware-halt" : "")

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
		u64 md_status =
			readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);

		cxl_cmd_err(cxlds->dev, mbox_cmd, md_status,
			    "mailbox queue busy");
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
		u64 md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);

		cxl_cmd_err(cxlds->dev, mbox_cmd, md_status, "mailbox timeout");
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

static int cxl_pci_mbox_send(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	int rc;

	mutex_lock_io(&cxlds->mbox_mutex);
	rc = __cxl_pci_mbox_send_cmd(cxlds, cmd);
	mutex_unlock(&cxlds->mbox_mutex);

	return rc;
}

static int cxl_pci_setup_mailbox(struct cxl_dev_state *cxlds)
{
	const int cap = readl(cxlds->regs.mbox + CXLDEV_MBOX_CAPS_OFFSET);
	unsigned long timeout;
	u64 md_status;

	timeout = jiffies + mbox_ready_timeout * HZ;
	do {
		md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
		if (md_status & CXLMDEV_MBOX_IF_READY)
			break;
		if (msleep_interruptible(100))
			break;
	} while (!time_after(jiffies, timeout));

	if (!(md_status & CXLMDEV_MBOX_IF_READY)) {
		cxl_err(cxlds->dev, md_status,
			"timeout awaiting mailbox ready");
		return -ETIMEDOUT;
	}

	/*
	 * A command may be in flight from a previous driver instance,
	 * think kexec, do one doorbell wait so that
	 * __cxl_pci_mbox_send_cmd() can assume that it is the only
	 * source for future doorbell busy events.
	 */
	if (cxl_pci_mbox_wait_for_doorbell(cxlds) != 0) {
		cxl_err(cxlds->dev, md_status, "timeout awaiting mailbox idle");
		return -ETIMEDOUT;
	}

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

static int wait_for_valid(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec, rc;
	u32 val;

	/*
	 * Memory_Info_Valid: When set, indicates that the CXL Range 1 Size high
	 * and Size Low registers are valid. Must be set within 1 second of
	 * deassertion of reset to CXL device. Likely it is already set by the
	 * time this runs, but otherwise give a 1.5 second timeout in case of
	 * clock skew.
	 */
	rc = pci_read_config_dword(pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &val);
	if (rc)
		return rc;

	if (val & CXL_DVSEC_MEM_INFO_VALID)
		return 0;

	msleep(1500);

	rc = pci_read_config_dword(pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &val);
	if (rc)
		return rc;

	if (val & CXL_DVSEC_MEM_INFO_VALID)
		return 0;

	return -ETIMEDOUT;
}

/*
 * Wait up to @mbox_ready_timeout for the device to report memory
 * active.
 */
static int wait_for_media_ready(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	bool active = false;
	u64 md_status;
	int rc, i;

	rc = wait_for_valid(cxlds);
	if (rc)
		return rc;

	for (i = mbox_ready_timeout; i; i--) {
		u32 temp;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &temp);
		if (rc)
			return rc;

		active = FIELD_GET(CXL_DVSEC_MEM_ACTIVE, temp);
		if (active)
			break;
		msleep(1000);
	}

	if (!active) {
		dev_err(&pdev->dev,
			"timeout awaiting memory active after %d seconds\n",
			mbox_ready_timeout);
		return -ETIMEDOUT;
	}

	md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
	if (!CXLMDEV_READY(md_status))
		return -EIO;

	return 0;
}

static int cxl_dvsec_ranges(struct cxl_dev_state *cxlds)
{
	struct cxl_endpoint_dvsec_info *info = &cxlds->info;
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	int hdm_count, rc, i;
	u16 cap, ctrl;

	if (!d)
		return -ENXIO;

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CAP_OFFSET, &cap);
	if (rc)
		return rc;

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CTRL_OFFSET, &ctrl);
	if (rc)
		return rc;

	if (!(cap & CXL_DVSEC_MEM_CAPABLE))
		return -ENXIO;

	/*
	 * It is not allowed by spec for MEM.capable to be set and have 0 legacy
	 * HDM decoders (values > 2 are also undefined as of CXL 2.0). As this
	 * driver is for a spec defined class code which must be CXL.mem
	 * capable, there is no point in continuing to enable CXL.mem.
	 */
	hdm_count = FIELD_GET(CXL_DVSEC_HDM_COUNT_MASK, cap);
	if (!hdm_count || hdm_count > 2)
		return -EINVAL;

	rc = wait_for_valid(cxlds);
	if (rc)
		return rc;

	info->mem_enabled = FIELD_GET(CXL_DVSEC_MEM_ENABLE, ctrl);

	for (i = 0; i < hdm_count; i++) {
		u64 base, size;
		u32 temp;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_HIGH(i), &temp);
		if (rc)
			return rc;

		size = (u64)temp << 32;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(i), &temp);
		if (rc)
			return rc;

		size |= temp & CXL_DVSEC_MEM_SIZE_LOW_MASK;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_BASE_HIGH(i), &temp);
		if (rc)
			return rc;

		base = (u64)temp << 32;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_BASE_LOW(i), &temp);
		if (rc)
			return rc;

		base |= temp & CXL_DVSEC_MEM_BASE_LOW_MASK;

		info->dvsec_range[i] = (struct range) {
			.start = base,
			.end = base + size - 1
		};

		if (size)
			info->ranges++;
	}

	return 0;
}

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

	cxlds->serial = pci_get_dsn(pdev);
	cxlds->cxl_dvsec = pci_find_dvsec_capability(
		pdev, PCI_DVSEC_VENDOR_ID_CXL, CXL_DVSEC_PCIE_DEVICE);
	if (!cxlds->cxl_dvsec)
		dev_warn(&pdev->dev,
			 "Device DVSEC not present, skip CXL.mem init\n");

	cxlds->wait_media_ready = wait_for_media_ready;

#ifdef CONFIG_EXMEM
	rc = cxl_dvsec_ranges(cxlds);
	if (rc)
		dev_warn(&pdev->dev,
			 "Failed to get DVSEC range information (%d)\n", rc);

	if (!rc) {
		rc = register_cxl_dvsec_ranges(cxlds);
		if (rc)
			pr_err("Failed to register cxl dvsec ranges (%d)\n", rc);
	}
#endif

	rc = cxl_setup_regs(pdev, CXL_REGLOC_RBI_MEMDEV, &map);
	if (rc)
		return rc;

	rc = cxl_map_regs(cxlds, &map);
	if (rc)
		return rc;

	/*
	 * If the component registers can't be found, the cxl_pci driver may
	 * still be useful for management functions so don't return an error.
	 */
	cxlds->component_reg_phys = CXL_RESOURCE_NONE;
	rc = cxl_setup_regs(pdev, CXL_REGLOC_RBI_COMPONENT, &map);
	if (rc)
		dev_warn(&pdev->dev, "No component registers (%d)\n", rc);

	cxlds->component_reg_phys = cxl_regmap_to_base(pdev, &map);

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

#ifndef CONFIG_EXMEM
	rc = cxl_dvsec_ranges(cxlds);
	if (rc)
		dev_warn(&pdev->dev,
			 "Failed to get DVSEC range information (%d)\n", rc);
#endif

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
static ssize_t cxl_mem_mode_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", cxl_mem_mode);
}

static ssize_t cxl_mem_mode_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int ret, tmp;
	enum CXL_MEM_MODE old = cxl_mem_mode;

	ret = kstrtoint(buf, 10, &tmp);
	if (ret < 0) {
		pr_err("CXL: bad cxl_mem_mode\n");
		return count;
	}

	if (tmp > CXL_MEM_MODE_MAX) {
		pr_err("CXL: bad cxl_mem_mode\n");
		return count;
	}

	if (tmp == old) {
		pr_info("CXL: same cxl_mem_mode, do nothing\n");
		return count;
	}

	if (tmp != CXL_MEM_MODE_ZONE_EXMEM) {
		pr_info("CXL: cxl_mem_mode is not ExMem zone, "
				"so change cxl_group_policy to zone\n");
		if (__change_cxl_group_policy(cxl_group_policy, CXL_GROUP_POLICY_ZONE))
			return count;
	}

	if (__change_cxl_mem_mode(old, tmp) < 0) {
		pr_err("CXL: failed to change cxl_mem_mode\n");
		return count;
	}

	return count;
}

static struct kobj_attribute cxl_mem_mode_attribute =
	__ATTR(cxl_mem_mode, 0664, cxl_mem_mode_show, cxl_mem_mode_store);

static ssize_t cxl_group_policy_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	if (cxl_group_policy == CXL_GROUP_POLICY_ZONE)
		return sprintf(buf, "zone\n");
	else if (cxl_group_policy == CXL_GROUP_POLICY_NODE)
		return sprintf(buf, "node\n");
	return sprintf(buf, "noop\n");
}

static ssize_t cxl_group_policy_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	enum CXL_GROUP_POLICY tmp;

	if (cxl_mem_mode != CXL_MEM_MODE_ZONE_EXMEM) {
		pr_info("CXL: cxl_group_policy can be changed "
				"only if cxl_mem_mode is 2\n");
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
		pr_err("CXL: failed to change cxl_group_policy\n");
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

static int exmem_init(void)
{
	int rc;

	rc = get_cxl_meminfo();
	if (rc) {
		pr_err("Failed to get cxl meminfo: %d\n", rc);
		return rc;
	}

	cxl_kobj = kobject_create_and_add("cxl", kernel_kobj);
	if (!cxl_kobj)
		return -ENOMEM;

	rc = sysfs_create_group(cxl_kobj, &attr_group);
	if (rc) {
		kobject_put(cxl_kobj);
		return rc;
	}

	rc = __change_cxl_mem_mode(cxl_mem_mode, CXL_MEM_MODE_ZONE_EXMEM);
	if (rc) {
		sysfs_remove_group(cxl_kobj, &attr_group);
		kobject_put(cxl_kobj);
		return rc;
	}

	return 0;
}

static void exmem_exit(void)
{
	if (cxl_mem_mode != CXL_MEM_MODE_SOFT_RESERVED)
		__change_cxl_mem_mode(cxl_mem_mode, CXL_MEM_MODE_SOFT_RESERVED);

	if (cxl_kobj) {
		sysfs_remove_group(cxl_kobj, &attr_group);
		kobject_put(cxl_kobj);
	}
}

static __init int exmem_cxl_pci_init(void)
{
	int rc;

	rc = exmem_init();
	if (rc)
		return rc;

	rc = pci_register_driver(&cxl_pci_driver);
	if (rc) {
		exmem_exit();
		return rc;
	}

	return 0;
}

static __exit void exmem_cxl_pci_exit(void)
{
	pci_unregister_driver(&cxl_pci_driver);
	exmem_exit();
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
