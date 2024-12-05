// SPDX-License-Identifier: GPL-2.0-only

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <cxlmem.h>
#include <hmu.h>
#include <cxl.h>
#include "core.h"

/* Let's assume that there's only one CHMU register set per device. */

static bool chmu_is_enabled(struct device *dev)
{
	u64 cfg;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	cfg = readq(hmu->base + CXL_HMU_CFG1_REG);
	if (cfg & CXL_HMU_CFG1_CONTROL_MSK)
		return true;
	else
		return false;
}

#define BITMAP_ATTR(name, n) 						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	struct cxl_hmu_info *info = dev_get_drvdata(dev);		\
	u64 reg, val;							\
	struct cxl_hmu *hmu = to_cxl_hmu(dev);				\
	reg = readq(hmu->base + CXL_HMU_COMMON_CAP_SIZE +		\
			info->caps[0].range_conf_bitmap_reg +		\
			CXL_HMU_RANGE_CONF_BITMAP_SIZE * (n));		\
	val = FIELD_GET(CXL_HMU_RANGE_CONF_BITMAP_MSK, reg);		\
	return sprintf(buf, "0x%llx\n", val);				\
}									\
static ssize_t name##_store(struct device *dev,			\
		struct device_attribute *attr,				\
		const char *buf, size_t count)				\
{									\
	struct cxl_hmu_info *info = dev_get_drvdata(dev);		\
	u64 cfg, val;							\
	int rc;								\
	struct cxl_hmu *hmu = to_cxl_hmu(dev);				\
	if (chmu_is_enabled(dev))					\
		return -EBUSY;						\
	rc = sscanf(buf, "0x%llx", &val);				\
	if (rc != 1)							\
		return -EINVAL;						\
	cfg = readq(hmu->base + CXL_HMU_COMMON_CAP_SIZE +		\
			info->caps[0].range_conf_bitmap_reg +		\
			CXL_HMU_RANGE_CONF_BITMAP_SIZE * (n));		\
	cfg &= ~CXL_HMU_RANGE_CONF_BITMAP_MSK;				\
	cfg |= FIELD_PREP(CXL_HMU_RANGE_CONF_BITMAP_MSK, val);		\
	writeq(cfg, hmu->base + CXL_HMU_COMMON_CAP_SIZE +		\
			info->caps[0].range_conf_bitmap_reg +		\
			CXL_HMU_RANGE_CONF_BITMAP_SIZE * (n));		\
	return count;							\
}									\
static DEVICE_ATTR_RW(name);

/* Assumption: 16GB capacity. If 32GB capacity, declare 4 bitmaps. */
BITMAP_ATTR(bitmap0, 0);
BITMAP_ATTR(bitmap1, 1);

static struct attribute *cxl_hmu_bitmap_attributes[] = {
	&dev_attr_bitmap0.attr,
	&dev_attr_bitmap1.attr,
	NULL,
};

static ssize_t irq_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", info->caps[0].irq);
}
static DEVICE_ATTR_RO(irq);

static ssize_t hotlist_overflow_sup_show(struct device *dev,
                 struct device_attribute *attr, char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", info->caps[0].hotlist_overflow_sup);
}
static DEVICE_ATTR_RO(hotlist_overflow_sup);

static ssize_t levels_crossing_sup_show(struct device *dev,
                 struct device_attribute *attr, char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", info->caps[0].hotlist_levels_crossing_sup);
}
static DEVICE_ATTR_RO(levels_crossing_sup);

static ssize_t epoch_type_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	if (info->caps[0].epoch_type == CXL_HMU_CAP1_EPOCH_TYPE_GLOBAL)
		return sprintf(buf, "%u (%s)\n", info->caps[0].epoch_type, "global");
	if (info->caps[0].epoch_type == CXL_HMU_CAP1_EPOCH_TYPE_PER_COUNTER)
		return sprintf(buf, "%u (%s)\n", info->caps[0].epoch_type, "per counter");
	return sprintf(buf, "%u (N/A)\n", info->caps[0].epoch_type);
}
static DEVICE_ATTR_RO(epoch_type);

static ssize_t tracked_m2s_req_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", info->caps[0].tracked_m2s_req);
}
static DEVICE_ATTR_RO(tracked_m2s_req);

static ssize_t max_epoch_len_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", info->caps[0].max_epoch_len);
}
static DEVICE_ATTR_RO(max_epoch_len);

static ssize_t min_epoch_len_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", info->caps[0].min_epoch_len);
}
static DEVICE_ATTR_RO(min_epoch_len);

static ssize_t hotlist_size_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", info->caps[0].hotlist_size);
}
static DEVICE_ATTR_RO(hotlist_size);

static ssize_t supported_unit_size_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "0x%x\n", info->caps[0].supported_unit_size);
}
static DEVICE_ATTR_RO(supported_unit_size);

static ssize_t supported_down_sampling_factor_show(struct device *dev,
                 struct device_attribute *attr, char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "0x%x\n", info->caps[0].supported_down_sampling_factor);
}
static DEVICE_ATTR_RO(supported_down_sampling_factor);

static ssize_t cap_flags_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "0x%x\n", info->caps[0].flags);
}
static DEVICE_ATTR_RO(cap_flags);

static ssize_t range_conf_bitmap_reg_show(struct device *dev,
                 struct device_attribute *attr, char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "0x%llx\n", info->caps[0].range_conf_bitmap_reg);
}
static DEVICE_ATTR_RO(range_conf_bitmap_reg);

static ssize_t hotlist_reg_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	return sprintf(buf, "0x%llx\n", info->caps[0].hotlist_reg);
}
static DEVICE_ATTR_RO(hotlist_reg);

static struct attribute *cxl_hmu_cap_attributes[] = {
	&dev_attr_irq.attr,
	&dev_attr_hotlist_overflow_sup.attr,
	&dev_attr_levels_crossing_sup.attr,
	&dev_attr_epoch_type.attr,
	&dev_attr_tracked_m2s_req.attr,
	&dev_attr_max_epoch_len.attr,
	&dev_attr_min_epoch_len.attr,
	&dev_attr_hotlist_size.attr,
	&dev_attr_supported_unit_size.attr,
	&dev_attr_supported_down_sampling_factor.attr,
	&dev_attr_cap_flags.attr,
	&dev_attr_range_conf_bitmap_reg.attr,
	&dev_attr_hotlist_reg.attr,
	NULL,
};

static ssize_t m2s_req_to_track_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG1_REG);
	val = FIELD_GET(CXL_HMU_CFG1_M2S_REQ_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t m2s_req_to_track_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	if (chmu_is_enabled(dev))
		return -EBUSY;

	sscanf(buf, "%llu", &val);

	cfg = readq(hmu->base + CXL_HMU_CFG1_REG);
	cfg &= ~CXL_HMU_CFG1_M2S_REQ_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG1_M2S_REQ_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG1_REG);

	return count;
}
static DEVICE_ATTR_RW(m2s_req_to_track);

static ssize_t flags_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG1_REG);
	val = FIELD_GET(CXL_HMU_CFG1_FLAGS_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t flags_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	if (chmu_is_enabled(dev))
		return -EBUSY;

	sscanf(buf, "%llu", &val);
	if (val & CXL_HMU_CFG1_FLAGS_RAND_DOWN_SAMPLING)
		return -EINVAL;

	if ((val & CXL_HMU_CFG1_FLAGS_INT_ON_OVERFLOW) &&
		!info->caps[0].hotlist_overflow_sup)
		return -EINVAL;

	if ((val & CXL_HMU_CFG1_FLAGS_INT_ON_LEVELS_CROSSING) &&
		!info->caps[0].hotlist_levels_crossing_sup)
		return -EINVAL;

	cfg = readq(hmu->base + CXL_HMU_CFG1_REG);
	cfg &= ~CXL_HMU_CFG1_FLAGS_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG1_FLAGS_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG1_REG);

	return count;
}
static DEVICE_ATTR_RW(flags);

static ssize_t hotness_threshold_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG1_REG);
	val = FIELD_GET(CXL_HMU_CFG1_HOTNESS_THRESHOLD_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t hotness_threshold_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val, stat, width;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	if (chmu_is_enabled(dev))
		return -EBUSY;

	sscanf(buf, "%llu", &val);

	stat = readq(hmu->base + CXL_HMU_STAT_REG);
	width = FIELD_GET(CXL_HMU_STAT_COUNTER_WIDTH_MSK, stat);
	if (val >= (1UL << width))
		return -EINVAL;

	cfg = readq(hmu->base + CXL_HMU_CFG1_REG);
	cfg &= ~CXL_HMU_CFG1_HOTNESS_THRESHOLD_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG1_HOTNESS_THRESHOLD_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG1_REG);

	return count;
}
static DEVICE_ATTR_RW(hotness_threshold);

static ssize_t unit_size_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG2_REG);
	val = FIELD_GET(CXL_HMU_CFG2_UNIT_SIZE_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t unit_size_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	struct cxl_hmu_info *info = dev_get_drvdata(dev);
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	if (chmu_is_enabled(dev))
		return -EBUSY;

	sscanf(buf, "%llu", &val);
	if (val < 8 || val > 31)
		return -EINVAL;

	if (!(info->caps[0].supported_unit_size & (1 << (val - 8))))
		return -EINVAL;

	cfg = readq(hmu->base + CXL_HMU_CFG2_REG);
	cfg &= ~CXL_HMU_CFG2_UNIT_SIZE_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG2_UNIT_SIZE_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG2_REG);

	return count;
}
static DEVICE_ATTR_RW(unit_size);

static ssize_t down_sampling_factor_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG2_REG);
	val = FIELD_GET(CXL_HMU_CFG2_DOWNSAMPLING_FACTOR_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t down_sampling_factor_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	if (chmu_is_enabled(dev))
		return -EBUSY;

	sscanf(buf, "%llu", &val);
	if (val >> 4)
		return -EINVAL;

	cfg = readq(hmu->base + CXL_HMU_CFG2_REG);
	cfg &= ~CXL_HMU_CFG2_DOWNSAMPLING_FACTOR_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG2_DOWNSAMPLING_FACTOR_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG2_REG);

	return count;
}
static DEVICE_ATTR_RW(down_sampling_factor);

static ssize_t reporting_mode_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG2_REG);
	val = FIELD_GET(CXL_HMU_CFG2_REPORTING_MODE_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t reporting_mode_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	if (chmu_is_enabled(dev))
		return -EBUSY;

	sscanf(buf, "%llu", &val);

	if (val != 0 && val != 1)
		return -EINVAL;

	cfg = readq(hmu->base + CXL_HMU_CFG2_REG);
	cfg &= ~CXL_HMU_CFG2_REPORTING_MODE_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG2_REPORTING_MODE_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG2_REG);

	return count;
}
static DEVICE_ATTR_RW(reporting_mode);

static ssize_t epoch_len_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG2_REG);
	val = FIELD_GET(CXL_HMU_CFG2_EPOCH_LEN_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t epoch_len_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	if (chmu_is_enabled(dev))
		return -EBUSY;

	sscanf(buf, "%llu", &val);

	cfg = readq(hmu->base + CXL_HMU_CFG2_REG);
	cfg &= ~CXL_HMU_CFG2_EPOCH_LEN_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG2_EPOCH_LEN_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG2_REG);

	return count;
}
static DEVICE_ATTR_RW(epoch_len);

static ssize_t control_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG1_REG);
	val = FIELD_GET(CXL_HMU_CFG1_CONTROL_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t control_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	sscanf(buf, "%llu", &val);

	if (val >> 2)
		return -EINVAL;

	cfg = readq(hmu->base + CXL_HMU_CFG1_REG);
	cfg &= ~CXL_HMU_CFG1_CONTROL_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG1_CONTROL_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG1_REG);

	return count;
}
static DEVICE_ATTR_RW(control);

static ssize_t hotlist_noti_threshold_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_CFG3_REG);
	val = FIELD_GET(CXL_HMU_CFG3_HOTLIST_NOTI_THRESHOLD_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t hotlist_noti_threshold_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	if (chmu_is_enabled(dev))
		return -EBUSY;

	sscanf(buf, "%llu", &val);

	cfg = readq(hmu->base + CXL_HMU_CFG3_REG);
	cfg &= ~CXL_HMU_CFG3_HOTLIST_NOTI_THRESHOLD_MSK;
	cfg |= FIELD_PREP(CXL_HMU_CFG3_HOTLIST_NOTI_THRESHOLD_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_CFG3_REG);

	return count;
}
static DEVICE_ATTR_RW(hotlist_noti_threshold);

static struct attribute *cxl_hmu_config_attributes[] = {
	&dev_attr_m2s_req_to_track.attr,
	&dev_attr_flags.attr,
	&dev_attr_hotness_threshold.attr,
	&dev_attr_unit_size.attr,
	&dev_attr_down_sampling_factor.attr,
	&dev_attr_reporting_mode.attr,
	&dev_attr_epoch_len.attr,
	&dev_attr_control.attr,
	&dev_attr_hotlist_noti_threshold.attr,
	NULL,
};

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_STAT_REG);
	val = FIELD_GET(CXL_HMU_STAT_STATUS_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}
DEVICE_ATTR_RO(status);

static ssize_t op_in_progress_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_STAT_REG);
	val = FIELD_GET(CXL_HMU_STAT_OP_IN_PROGRESS_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}
DEVICE_ATTR_RO(op_in_progress);

static ssize_t counter_width_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_STAT_REG);
	val = FIELD_GET(CXL_HMU_STAT_COUNTER_WIDTH_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}
DEVICE_ATTR_RO(counter_width);

static ssize_t overflow_itr_status_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_STAT_REG);
	val = FIELD_GET(CXL_HMU_STAT_OVERFLOW_INT_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t overflow_itr_status_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	sscanf(buf, "%llu", &val);

	if (val >> 2)
		return -EINVAL;

	cfg = readq(hmu->base + CXL_HMU_STAT_REG);
	cfg &= ~CXL_HMU_STAT_OVERFLOW_INT_MSK;
	cfg |= FIELD_PREP(CXL_HMU_STAT_OVERFLOW_INT_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_STAT_REG);

	return count;
}
DEVICE_ATTR_RW(overflow_itr_status);

static struct attribute *cxl_hmu_stat_attributes[] = {
	&dev_attr_status.attr,
	&dev_attr_op_in_progress.attr,
	&dev_attr_counter_width.attr,
	&dev_attr_overflow_itr_status.attr,
	NULL,
};

static ssize_t head_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_HOTLIST_HEAD_TAIL_REG);
	val = FIELD_GET(CXL_HMU_HOTLIST_HEAD_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}

static ssize_t head_store(struct device *dev, struct device_attribute *attr,
                 const char *buf, size_t count)
{
	u64 cfg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	sscanf(buf, "%llu", &val);

	cfg = readq(hmu->base + CXL_HMU_HOTLIST_HEAD_TAIL_REG);
	cfg &= ~CXL_HMU_HOTLIST_HEAD_MSK;
	cfg |= FIELD_PREP(CXL_HMU_HOTLIST_HEAD_MSK, val);
	writeq(cfg, hmu->base + CXL_HMU_HOTLIST_HEAD_TAIL_REG);

	return count;
}
DEVICE_ATTR_RW(head);

static ssize_t tail_show(struct device *dev, struct device_attribute *attr,
                 char *buf)
{
	u64 reg, val;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	reg = readq(hmu->base + CXL_HMU_HOTLIST_HEAD_TAIL_REG);
	val = FIELD_GET(CXL_HMU_HOTLIST_TAIL_MSK, reg);

	return sprintf(buf, "%llu\n", val);
}
DEVICE_ATTR_RO(tail);


static ssize_t trigger_read_hotlist_store(struct device *dev,
               struct device_attribute *attr, const char *buf, size_t count)
{
	u64 val, hot, entry, idx, dpa, hpa;
	u16 head, tail, buf_head, buf_tail, buf_count = 0;
	struct cxl_hmu *hmu = to_cxl_hmu(dev);
	struct cxl_hmu_info *info = dev->driver_data;
	struct pci_dev *pdev = to_pci_dev(info->dev->parent);
	struct cxl_dev_state *cxlds = pci_get_drvdata(pdev);
	struct cxl_memdev *cxlmd = cxlds->cxlmd;

	sscanf(buf, "%llu", &val);
	if (val) {
		val = readq(hmu->base + CXL_HMU_STAT_REG);
		info->status[0].counter_width =
			FIELD_GET(CXL_HMU_STAT_COUNTER_WIDTH_MSK, val);
		info->status[0].overflow_itr_status =
			FIELD_GET(CXL_HMU_STAT_OVERFLOW_INT_MSK, val);

		val = readq(hmu->base + CXL_HMU_CFG2_REG);
		info->config[0].unit_size = FIELD_GET(CXL_HMU_CFG2_UNIT_SIZE_MSK, val);

		spin_lock(&info->hot_buf_lock);
		hot = readq(hmu->base + CXL_HMU_HOTLIST_HEAD_TAIL_REG);
		head = FIELD_GET(CXL_HMU_HOTLIST_HEAD_MSK, hot);
		tail = FIELD_GET(CXL_HMU_HOTLIST_TAIL_MSK, hot);
		buf_head = info->hot_buf_head;
		buf_tail = info->hot_buf_tail;

		pr_info("trigger device's hotlist entry read\n");

		/* 1) Save device's hotlist entry to driver's buffer */
		while ((head != tail) &&
			(buf_head != ((buf_tail + 1) % 1024))) {
			entry = readq(hmu->base + CXL_HMU_COMMON_CAP_SIZE +
					info->caps[0].hotlist_reg + head * 8);
			idx = entry >> info->status[0].counter_width;
			dpa = idx * (1 << info->config[0].unit_size);
			hpa = cxl_memdev_dpa_to_hpa(cxlmd, dpa);

			if (hpa == ULLONG_MAX)
				dev_err(&cxlmd->dev, "Bad dpa-to-hpa translation\n");
			else {
				info->hot_buf[buf_tail] = hpa;
				buf_tail += 1;
				buf_tail %= 1024;
			}

			head += 1;
			head %= info->caps[0].hotlist_size;
		}

		/* 2) Write device's head and tail */
		hot = 0;
		hot |= FIELD_PREP(CXL_HMU_HOTLIST_HEAD_MSK, head);
		hot |= FIELD_PREP(CXL_HMU_HOTLIST_TAIL_MSK, tail);
		writeq(hot, hmu->base + CXL_HMU_HOTLIST_HEAD_TAIL_REG);

		hot = readq(hmu->base + CXL_HMU_HOTLIST_HEAD_TAIL_REG);
		head = FIELD_GET(CXL_HMU_HOTLIST_HEAD_MSK, hot);
		tail = FIELD_GET(CXL_HMU_HOTLIST_TAIL_MSK, hot);

		pr_info("After read, Device's head : %d, tail : %d\n", head, tail);

		/* 3) Print driver's buffer */
		pr_info("start driver's hotlist buffer read\n");

		while (buf_head != buf_tail) {
			pr_info("entry : 0x%llx\n", info->hot_buf[buf_head]);
			buf_head++;
			buf_head %= 1024;
			buf_count++;
		}
		pr_info("total %u hot list entries printed, buf head: %u, buf tail: %u\n",
			 buf_count, buf_head, buf_tail);

		/* 4) Save renewed Driver's hotlist buffer head and tail */
		info->hot_buf_head = buf_head;
		info->hot_buf_tail = buf_tail;

		spin_unlock(&info->hot_buf_lock);
	}

	return count;
}
DEVICE_ATTR_WO(trigger_read_hotlist);

static struct attribute *cxl_hmu_hotlist_attributes[] = {
	&dev_attr_head.attr,
	&dev_attr_tail.attr,
	&dev_attr_trigger_read_hotlist.attr,
	NULL,
};

static struct attribute_group cxl_hmu_bitmap_attribute_group = {
	.name = "bitmap",
	.attrs = cxl_hmu_bitmap_attributes,
};

static struct attribute_group cxl_hmu_cap_attribute_group = {
	.name = "cap",
	.attrs = cxl_hmu_cap_attributes,
};

static struct attribute_group cxl_hmu_config_attribute_group = {
	.name = "config",
	.attrs = cxl_hmu_config_attributes,
};

static struct attribute_group cxl_hmu_stat_attribute_group = {
	.name = "stat",
	.attrs = cxl_hmu_stat_attributes,
};

static struct attribute_group cxl_hmu_hotlist_attribute_group = {
	.name = "hotlist",
	.attrs = cxl_hmu_hotlist_attributes,
};

static const struct attribute_group *cxl_hmu_attribute_groups[] = {
	&cxl_hmu_bitmap_attribute_group,
	&cxl_hmu_cap_attribute_group,
	&cxl_hmu_config_attribute_group,
	&cxl_hmu_stat_attribute_group,
	&cxl_hmu_hotlist_attribute_group,
	NULL,
};

static void cxl_hmu_release(struct device *dev)
{
	struct cxl_hmu *hmu = to_cxl_hmu(dev);

	kfree(hmu);
}

const struct device_type cxl_hmu_type = {
	.name = "cxl_hmu",
	.release = cxl_hmu_release,
	.groups = cxl_hmu_attribute_groups,
};

static void remove_dev(void *dev)
{
	device_unregister(dev);
}

int devm_cxl_hmu_add(struct device *parent, struct cxl_hmu_regs *regs,
		     int assoc_id, int index, enum cxl_hmu_type type)
{
	struct cxl_hmu *hmu;
	struct device *dev;
	int rc;

	hmu = kzalloc(sizeof(*hmu), GFP_KERNEL);
	if (!hmu)
		return -ENOMEM;

	hmu->assoc_id = assoc_id;
	hmu->index = index;
	hmu->type = type;
	hmu->base = regs->hmu;
	dev = &hmu->dev;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = parent;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_hmu_type;
	switch (hmu->type) {
	case CXL_HMU_MEMDEV:
		rc = dev_set_name(dev, "hmu_mem%d.%d", assoc_id, index);
		break;
	}
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	return devm_add_action_or_reset(parent, remove_dev, dev);

err:
	put_device(&hmu->dev);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_hmu_add, CXL);

resource_size_t cxl_hmu_get_reg_size(struct cxl_hmu_regs *regs)
{
	u64 val;
	u8 num_chmus;
	u16 chmu_instance_len;

	val = readq(regs->hmu + CXL_HMU_COMMON_CAP1_REG);
	num_chmus = FIELD_GET(CXL_HMU_COMMON_CAP1_CHMU_NUM_MSK, val);
	if (num_chmus > CXL_HMU_MAX_CNT)
		return CXL_RESOURCE_NONE;

	val = readq(regs->hmu + CXL_HMU_COMMON_CAP2_REG);
	chmu_instance_len = FIELD_GET(CXL_HMU_COMMON_CAP2_INSTANCE_LEN_MSK, val);

	return CXL_HMU_COMMON_CAP_SIZE + chmu_instance_len * num_chmus;
}
EXPORT_SYMBOL_NS_GPL(cxl_hmu_get_reg_size, CXL);
