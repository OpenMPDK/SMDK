#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sizes.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/numa.h>
#include "cxlmem.h"

#define CXL_NAME_BUF_SIZE	(16)

enum DEVICE_STATE {
	ERROR = 0,
	OFFLINE = 1,
	ONLINE = 2,
	NOTMOVABLE = 3,
};

struct cxl_memblk {
	char dev_name[CXL_NAME_BUF_SIZE];
	char memdev_name[CXL_NAME_BUF_SIZE];
	u64 start;
	u64 end;
	u64	size;
	int socket_id;
	int node_id;
	bool from_srat;
	int state;
	struct resource *res;
	struct kobject *kobj;
};

struct cxl_meminfo {
	int nr_blks;
	struct cxl_memblk blk[MAX_NUMNODES];
};

static struct cxl_meminfo cxl_meminfo;
static DEFINE_MUTEX(cxl_meminfo_lock);
static int __add_cxl_memory(struct cxl_memblk *cmb, int target_nid);
static int __remove_cxl_memory(struct cxl_memblk *cmb);

/* Memory resource name used for add_memory_driver_managed(). */
static const char *smdk_res_name = "System RAM (cxl)";

#define KOBJ_RELEASE(_name) \
	{ kobject_put(_name); _name = NULL;}

#define CXL_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define CXL_ATTR(_name) \
	static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

static struct cxl_memblk *kobj_to_memblk(struct kobject *kobj)
{
	int i;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].kobj == kobj)
			return &cxl_meminfo.blk[i];
	}

	return NULL;
}

static ssize_t start_address_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct cxl_memblk *cmb;

	cmb = kobj_to_memblk(kobj);
	if (!cmb)
		return -EINVAL;

	return sysfs_emit(buf, "0x%llx\n", cmb->start);
}
CXL_ATTR_RO(start_address);

static ssize_t size_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct cxl_memblk *cmb;

	cmb = kobj_to_memblk(kobj);
	if (!cmb)
		return -EINVAL;

	return sysfs_emit(buf, "0x%llx\n", cmb->size);
}
CXL_ATTR_RO(size);

static ssize_t node_id_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct cxl_memblk *cmb;

	cmb = kobj_to_memblk(kobj);
	if (!cmb)
		return -EINVAL;

	return sysfs_emit(buf, "%d\n", cmb->node_id);
}

static ssize_t node_id_store(struct kobject *kobj,
	       struct kobj_attribute *attr, const char *buf, size_t len)
{
	int rc, target_node_id;
	struct cxl_memblk *cmb;

	cmb = kobj_to_memblk(kobj);
	if (!cmb)
		return -EINVAL;

	rc = kstrtoint(buf, 20, &target_node_id);
	if (target_node_id < -1) {
		pr_err("CXL: Node id is less than -1.\n");
		return -EINVAL;
	}

	if (cmb->state == ERROR) {
		pr_err("CXL: Device state is error\n");
		return -EINVAL;
	}

	if (target_node_id == -1) {
		rc = __remove_cxl_memory(cmb);
		if (rc) {
			pr_err("CXL: device remove fail\n");
			return rc;
		}
		return len;
	}

	if (cmb->from_srat == false && target_node_id != cmb->socket_id) {
		pr_err("CXL: Notmovable Device should be online with dev node id\n");
		return -EINVAL;
	}

	if (!node_state(target_node_id, N_POSSIBLE)) {
		pr_err("CXL: Node id is not possible.\n");
		return -EINVAL;
	}

	if (cmb->node_id == target_node_id) {
		pr_info("CXL: device is already in node id: %d\n", target_node_id);
		return len;
	}

	if (cmb->state != OFFLINE) {
		rc = __remove_cxl_memory(cmb);
		if (rc) {
			pr_err("CXL: device remove fail\n");
			return rc;
		}
	}

	rc = __add_cxl_memory(cmb, target_node_id);
	if (rc) {
		pr_err("CXL: device add fail\n");
		return rc;
	}

	return len;
}
CXL_ATTR(node_id);

static ssize_t socket_id_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct cxl_memblk *cmb;

	cmb = kobj_to_memblk(kobj);
	if (!cmb)
		return -EINVAL;

	return sysfs_emit(buf, "%d\n", cmb->socket_id);
}
CXL_ATTR_RO(socket_id);

static ssize_t state_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct cxl_memblk *cmb;
	char state_str[20];

	cmb = kobj_to_memblk(kobj);
	if (!cmb)
		return -EINVAL;

	switch (cmb->state) {
	case ONLINE:
		snprintf(state_str, 20, "online");
		break;

	case OFFLINE:
		snprintf(state_str, 20, "offline");
		break;

	case ERROR:
		snprintf(state_str, 20, "error");
		break;

	case NOTMOVABLE:
		snprintf(state_str, 20, "notmovable");
		break;

	default:
		break;
	}

	return sysfs_emit(buf, "%s\n", state_str);
}
CXL_ATTR_RO(state);

static struct attribute *cxl_attrs[] = {
	&start_address_attr.attr,
	&size_attr.attr,
	&node_id_attr.attr,
	&socket_id_attr.attr,
	&state_attr.attr,
	NULL,
};

static const struct attribute_group cxl_attr_group = {
	.attrs = cxl_attrs,
};

static struct kobject *cxl_kobj;

static struct kobject *cxl_devices_kobj;
static struct kobject *cxl_nodes_kobj;

static struct kobject *cxl_devices_kobjs[MAX_NUMNODES];
static struct kobject *cxl_nodes_kobjs[MAX_NUMNODES];

static int try_remove_node_kobj(struct cxl_memblk *remove_cmb)
{
	int i;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		struct cxl_memblk *cmb = &cxl_meminfo.blk[i];

		if (remove_cmb == cmb)
			continue;
		if (remove_cmb->node_id == cmb->node_id)
			return -EEXIST;
	}

	KOBJ_RELEASE(cxl_nodes_kobjs[remove_cmb->node_id]);
	return 0;
}

static int register_cxl_device_link(struct cxl_memblk *cmb, int target_node_id)
{
	int ret;
	char node_name[CXL_NAME_BUF_SIZE];

	if (!cxl_nodes_kobjs[target_node_id]) {
		snprintf(node_name, CXL_NAME_BUF_SIZE, "node%d", target_node_id);
		cxl_nodes_kobjs[target_node_id] = kobject_create_and_add(node_name, cxl_nodes_kobj);
		if (!cxl_nodes_kobjs[target_node_id])
			return -ENOMEM;
	}

	ret = sysfs_create_link_nowarn(cxl_nodes_kobjs[target_node_id], cmb->kobj, cmb->dev_name);

	return ret;
}

static void unregister_cxl_device_link(struct cxl_memblk *cmb)
{
	sysfs_remove_link(cxl_nodes_kobjs[cmb->node_id], cmb->dev_name);

	if (!try_remove_node_kobj(cmb))
		pr_info("CXL: node is deleted\n");
}


static int exmem_sysfs_add_meminfo(int info_idx, struct cxl_memblk *cmb)
{
	int retval;

	cxl_devices_kobjs[info_idx] = kobject_create_and_add(cmb->dev_name, cxl_devices_kobj);
	if (!cxl_devices_kobjs[info_idx])
		return -ENOMEM;

	retval = sysfs_create_group(cxl_devices_kobjs[info_idx], &cxl_attr_group);
	if (retval) {
		KOBJ_RELEASE(cxl_devices_kobjs[info_idx]);
		return retval;
	}

	cmb->kobj = cxl_devices_kobjs[info_idx];

	return retval;
}

static int __init exmem_sysfs_init(void)
{
	int i;

	cxl_kobj = kobject_create_and_add("cxl", kernel_kobj);
	if (!cxl_kobj)
		return -ENOMEM;

	cxl_devices_kobj = kobject_create_and_add("devices", cxl_kobj);
	if (!cxl_devices_kobj) {
		KOBJ_RELEASE(cxl_kobj);
		return -ENOMEM;
	}

	cxl_nodes_kobj = kobject_create_and_add("nodes", cxl_kobj);
	if (!cxl_nodes_kobj) {
		KOBJ_RELEASE(cxl_devices_kobj);
		KOBJ_RELEASE(cxl_kobj);
		return -ENOMEM;
	}

	for (i = 0; i < MAX_NUMNODES; i++) {
		cxl_nodes_kobjs[i] = NULL;
		cxl_devices_kobjs[i] = NULL;
	}

	return 0;
}

static void exmem_sysfs_exit(void)
{
	int i;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		struct cxl_memblk *cmb = &cxl_meminfo.blk[i];

		__remove_cxl_memory(cmb);
		if(cxl_devices_kobjs[i]) {
			sysfs_remove_link(cmb->kobj, cmb->memdev_name);
			sysfs_remove_group(cxl_devices_kobjs[i], &cxl_attr_group);
			KOBJ_RELEASE(cxl_devices_kobjs[i]);
		}
	}
	if (cxl_devices_kobj)
		KOBJ_RELEASE(cxl_devices_kobj);

	if (cxl_nodes_kobj)
		KOBJ_RELEASE(cxl_nodes_kobj);

	if (cxl_kobj)
		KOBJ_RELEASE(cxl_kobj);
}
/**
 * __add_cxl_memory - Add cxl.mem range to system memory
 * @cmb: information descriptor for cxl memory block
 * @target_id: node id to be belong to
 */
static int __add_cxl_memory(struct cxl_memblk *cmb, int target_node_id)
{
	int rc = 0;
	int mhp_value = MHP_MERGE_RESOURCE | MHP_EXMEM;
	struct resource *res;

	if (!cmb) {
		pr_err("CXL: cxl memblock is null\n");
		return -EINVAL;
	}

	if (target_node_id < 0 || MAX_NUMNODES <= target_node_id ||
			!node_state(target_node_id, N_POSSIBLE)) {
		pr_err("CXL: invalid node id: %d\n", target_node_id);
		return -EINVAL;
	}

	pr_info("CXL: request mem region: [mem %#010llx-%#010llx]\n", cmb->start,
			cmb->end);

	res = request_mem_region(cmb->start, cmb->size, "cxl");
	if (!res) {
		pr_err("CXL: failed to request mem region\n");
		return -EBUSY;
	}

	res->flags = IORESOURCE_SYSTEM_RAM;
	cmb->res = res;

	pr_info("CXL: add subzone: node_id: %d [mem %#010llx-%#010llx]\n", target_node_id,
			cmb->start, cmb->end);

	rc = add_subzone(target_node_id, cmb->start, cmb->end);
	if (rc) {
		pr_err("CXL: failed to add subzone\n");
		goto err_add_subzone;
	}

	rc = add_memory_driver_managed(target_node_id, cmb->start, cmb->size, smdk_res_name,
			mhp_value);
	if (rc) {
		pr_err("CXL: failed to add memory with %d\n", rc);
		goto err_add_memory;
	}

	rc = register_cxl_device_link(cmb, target_node_id);
	if (rc) {
		pr_err("CXL: failed to register device link\n");
		goto err_device_link;
	}

	cmb->node_id = target_node_id;
	if (cmb->from_srat)
		cmb->state = ONLINE;
	else
		cmb->state = NOTMOVABLE;

	return 0;

err_device_link:
	if (offline_and_remove_memory(cmb->start, cmb->size))
		pr_err("CXL: failed to remove memory\n");

err_add_memory:
	if (remove_subzone(cmb->node_id, cmb->start, cmb->end))
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

	if (!cmb) {
		pr_err("CXL: cxl memblock is null\n");
		return -EINVAL;
	}

	if (cmb->state == OFFLINE) {
		pr_info("CXL: cxl memblock is already removed\n");
		return 0;
	}

	unregister_cxl_device_link(cmb);

	pr_info("CXL: remove [mem %#010llx-%#010llx]\n", cmb->start, cmb->end);

	rc = offline_and_remove_memory(cmb->start, cmb->size);
	if (rc) {
		pr_err("CXL: failed to offline and remove memory\n");
		return rc;
	}

	pr_info("CXL: remove subzone: node_id: %d [mem %#010llx-%#010llx]\n",
			cmb->node_id, cmb->start, cmb->end);

	rc = remove_subzone(cmb->node_id, cmb->start, cmb->end);
	if (rc)
		pr_warn("CXL: failed to remove subzone\n");

	pr_info("CXL: release resource %pr\n", cmb->res);

	release_resource(cmb->res);
	kfree(cmb->res);
	cmb->res = NULL;
	cmb->state = OFFLINE;
	cmb->node_id = -1;

	return rc;
}

static void print_cxl_meminfo(void)
{
	int i, cnt = 0;

	for (i = 0; i < cxl_meminfo.nr_blks; i++) {
		if (cxl_meminfo.blk[i].node_id == NUMA_NO_NODE)
			continue;

		pr_info("CXL: node_id: %d, dev node_id: %d, [mem %#010llx-%#010llx]\n",
				cxl_meminfo.blk[i].node_id, cxl_meminfo.blk[i].socket_id,
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
	struct cxl_memblk *cmb = data;

	pr_info("CXL: soft reserved: [mem %#010llx-%#010llx]\n",
			res->start, res->end);

	if (res->start <= cmb->start && cmb->end <= res->end)
		return 0;

	pr_info("CXL: out of range: [mem %#010llx-%#010llx]\n",
			cmb->start, cmb->end);

	return -1;
}

static struct cxl_memblk *add_cxl_meminfo(u64 start, u64 end, int node_id, bool from_srat)
{
	struct cxl_memblk *cmb = NULL;

	if (node_id < 0 || cxl_meminfo.nr_blks >= MAX_NUMNODES)
		return NULL;

	cmb = &cxl_meminfo.blk[cxl_meminfo.nr_blks];
	cmb->start = start;
	cmb->end = end;
	cmb->size = end - start + 1;
	cmb->socket_id = 0;
	cmb->from_srat = from_srat;
	cmb->state = OFFLINE;

	if (walk_iomem_res_desc(IORES_DESC_SOFT_RESERVED,
		IORESOURCE_MEM, cmb->start, cmb->end, cmb, __check_cxl_range)) {
		return NULL;
	}

	snprintf(cmb->dev_name, CXL_NAME_BUF_SIZE, "cxl%d", cxl_meminfo.nr_blks);

	if (exmem_sysfs_add_meminfo(cxl_meminfo.nr_blks, cmb))
		return NULL;

	cxl_meminfo.nr_blks++;

	if (__add_cxl_memory(cmb, node_id)) {
		pr_err("CXL: add cxl memory fail\n");
		return NULL;
	}

	pr_info("CXL: add meminfo: node_id: %d, [mem %#010llx-%#010llx]\n",
			node_id, cmb->start, cmb->end);

	return cmb;
}

static int get_cxl_meminfo(void)
{
	int nr_blks = numa_get_reserved_meminfo_cnt();
	int node_id;
	u64 start, end;
	int rc, i;
	struct cxl_memblk *cmb;

	for (i = 0; i < nr_blks; i++) {
		rc = numa_get_reserved_meminfo(i, &node_id, &start, &end);
		if (rc) {
			pr_err("CXL: Failed to get cxl meminfo: (%d)\n", rc);
			return -1;
		}

		cmb = find_cxl_meminfo(start, end - 1);
		if (cmb) {
			pr_warn("CXL: same info, [mem %#010llx-%#010llx]\n", start, end);
			continue;
		}

		if (!add_cxl_meminfo(start, end - 1, 0, true)) {
			pr_err("CXL: failed to add cxl meminfo\n");
			continue;
		}
	}
	print_cxl_meminfo();

	return 0;
}

static int update_or_add_cxl_meminfo(u64 start, u64 end, int dev_numa_node)
{
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
			cmb->socket_id = dev_numa_node;

			if (cmb->node_id != cmb->socket_id) {
				rc = __remove_cxl_memory(cmb);
				if (rc)
					goto out;

				rc = __add_cxl_memory(cmb, cmb->socket_id);
				if (rc)
					goto out;
			}
		}
	} else {
		cmb = add_cxl_meminfo(start, end,
			dev_numa_node == NUMA_NO_NODE ? 0 : dev_numa_node, false);
		if (!cmb) {
			mutex_unlock(&cxl_meminfo_lock);
			pr_err("CXL: Failed to add cxl meminfo\n");
			return -EINVAL;
		}
		cmb->socket_id = dev_numa_node;
	}

out:
	mutex_unlock(&cxl_meminfo_lock);
	return rc;
}

int register_cxl_dvsec_ranges(struct cxl_dev_state *cxlds)
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

		rc = update_or_add_cxl_meminfo(info->dvsec_range[i].start,
				info->dvsec_range[i].end, dev->numa_node);
		if (!rc)
			return rc;
	}

	print_cxl_meminfo();
	return 0;
}

static int register_cxl_cfmws_ranges(struct cxl_decoder *cxld)
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

int add_cxl_info_cfmws(struct device *match, void *data)
{
	struct cxl_decoder *cxld;
	int rc;

	if (!is_root_decoder(match))
		return 0;

	cxld = to_cxl_decoder(match);
	if (!(cxld->flags & CXL_DECODER_F_RAM))
		return 0;

	pr_info("CXL: %s node: %d range %pr\n", dev_name(&cxld->dev),
			phys_to_target_node(cxld->platform_res.start),
			&cxld->platform_res);

	rc = register_cxl_cfmws_ranges(cxld);
	if (rc)
		return rc;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(add_cxl_info_cfmws, CXL);

void cxl_map_memdev_to_cxlmemblk(struct cxl_memdev *cxlmd, 
		struct cxl_endpoint_dvsec_info *info)
{
	int rc = 0;
	int memdev_id = cxlmd->id;
	struct cxl_memblk *cmb = NULL;
	u64 start, end;
	int i;

	for (i = 0; i < info->ranges; i++) {
		start = info->dvsec_range[i].start;
		end = info->dvsec_range[i].end;
			
		if (start == 0 || end == 0)
			continue;

		cmb = find_cxl_meminfo(start, end);

		if (!cmb) {
			pr_info("can't find cxl_memblk.\n");
			continue;
		}

		if (cmb->state != ERROR) {
			snprintf(cmb->memdev_name, CXL_NAME_BUF_SIZE,
					"mem%d", memdev_id);
			rc = sysfs_create_link_nowarn(cmb->kobj, &cxlmd->dev.kobj,
					cmb->memdev_name);
			if (rc)
				pr_warn("Failed to create link %s for %s (%d)\n", cmb->memdev_name, cmb->dev_name, rc);
		}
	}
}

int exmem_init(void)
{
	int rc;

	rc = exmem_sysfs_init();
	if (rc)
		return rc;

	rc = get_cxl_meminfo();
	if (rc) {
		pr_err("Failed to get cxl meminfo: %d\n", rc);
		return rc;
	}

	return 0;
}

void exmem_exit(void)
{
	exmem_sysfs_exit();
}
