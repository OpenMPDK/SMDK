/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CXL Specification rev 3.1 ECN v0.9 (CHMU Register Interface)
 */
#ifndef CXL_HMU_H
#define CXL_HMU_H
#include <linux/device.h>

#define CXL_HMU_COMMON_CAP_SIZE 0x10
#define CXL_HMU_RANGE_CONF_BITMAP_SIZE	0x08

#define CXL_HMU_COMMON_CAP1_REG	0x0
#define   CXL_HMU_COMMON_CAP1_VERSION_MSK		GENMASK_ULL(3, 0)
#define   CXL_HMU_COMMON_CAP1_CHMU_NUM_MSK		GENMASK_ULL(15, 8)
#define CXL_HMU_COMMON_CAP2_REG	0x8
#define   CXL_HMU_COMMON_CAP2_INSTANCE_LEN_MSK	GENMASK_ULL(15, 0)

#define CXL_HMU_CAP1_REG		0x10
#define   CXL_HMU_CAP1_INT_MSG_NUM_MSK			GENMASK_ULL(3, 0)
#define   CXL_HMU_CAP1_HOTLIST_OVERFLOW_SUP				BIT_ULL(4)
#define   CXL_HMU_CAP1_HOTLIST_LEVELS_CROSSING_SUP		BIT_ULL(5)
#define   CXL_HMU_CAP1_EPOCH_TYPE_MSK			GENMASK_ULL(7, 6)
#define     CXL_HMU_CAP1_EPOCH_TYPE_GLOBAL		0
#define     CXL_HMU_CAP1_EPOCH_TYPE_PER_COUNTER	1
#define   CXL_HMU_CAP1_TRACKED_M2S_REQ_MSK		GENMASK_ULL(15, 8)
#define     CXL_HMU_M2S_REQ_NONTEE_RDONLY		BIT(0)
#define     CXL_HMU_M2S_REQ_NONTEE_WRONLY		BIT(1)
#define     CXL_HMU_M2S_REQ_NONTEE_RDWR			BIT(2)
#define     CXL_HMU_M2S_REQ_RD					BIT(3)
#define     CXL_HMU_M2S_REQ_WR					BIT(4)
#define     CXL_HMU_M2S_REQ_ALL					BIT(5)
#define   CXL_HMU_CAP1_MAX_EPOCH_LEN_MSK		GENMASK_ULL(31, 16)
#define   CXL_HMU_CAP1_MIN_EPOCH_LEN_MSK		GENMASK_ULL(47, 32)
#define   CXL_HMU_CAP1_HOTLIST_SIZE_MSK			GENMASK_ULL(63, 48)

#define CXL_HMU_CAP2_REG		0x18
#define   CXL_HMU_CAP2_UNIT_SIZE_SUP_MSK		GENMASK_ULL(31, 0)
#define   CXL_HMU_CAP2_DOWNSAMPLING_SUP_MSK		GENMASK_ULL(47, 32)
#define   CXL_HMU_CAP2_CAP_FLAGS_MSK			GENMASK_ULL(63, 48)
#define     CXL_HMU_CAP2_CAP_FLAGS_EPOCH_BASED			BIT(0)
#define     CXL_HMU_CAP2_CAP_FLAGS_ALWAYS_ON			BIT(1)
#define     CXL_HMU_CAP2_CAP_FLAGS_RAND_DOWNSAMPLING	BIT(2)
#define     CXL_HMU_CAP2_CAP_FLAGS_ADDRESS_OVERLAP		BIT(3)
#define     CXL_HMU_CAP2_CAP_FLAGS_POSTPONED_INSERT		BIT(4)

#define CXL_HMU_CAP3_REG		0x20
#define   CXL_HMU_CAP3_RANGE_CONF_BITMAP_REG_OFFSET_MSK	GENMASK_ULL(63, 0)
#define CXL_HMU_RANGE_CONF_BITMAP_MSK			GENMASK_ULL(63, 0)

#define CXL_HMU_CAP4_REG		0x28
#define   CXL_HMU_CAP4_HOTLIST_REG_OFFSET_MSK	GENMASK_ULL(63, 0)

#define CXL_HMU_CFG1_REG		0x50
#define   CXL_HMU_CFG1_M2S_REQ_MSK				GENMASK_ULL(7, 0)
#define   CXL_HMU_CFG1_FLAGS_MSK				GENMASK_ULL(15, 8)
#define     CXL_HMU_CFG1_FLAGS_RAND_DOWN_SAMPLING		BIT(0)
#define     CXL_HMU_CFG1_FLAGS_INT_ON_OVERFLOW			BIT(1)
#define     CXL_HMU_CFG1_FLAGS_INT_ON_LEVELS_CROSSING	BIT(2)
#define   CXL_HMU_CFG1_CONTROL_MSK				GENMASK_ULL(31, 16)
#define     CXL_HMU_CFG1_CONTROL_ENABLE					BIT(0)
#define     CXL_HMU_CFG1_CONTROL_RESET					BIT(1)
#define   CXL_HMU_CFG1_HOTNESS_THRESHOLD_MSK	GENMASK_ULL(63, 32)
#define CXL_HMU_CFG2_REG		0x58
#define   CXL_HMU_CFG2_UNIT_SIZE_MSK			GENMASK_ULL(31, 0)
#define   CXL_HMU_CFG2_DOWNSAMPLING_FACTOR_MSK	GENMASK_ULL(39, 32)
#define   CXL_HMU_CFG2_REPORTING_MODE_MSK		GENMASK_ULL(47, 40)
#define     CXL_HMU_CFG2_REPORTING_MODE_EPOCH_BASED		0
#define     CXL_HMU_CFG2_REPORTING_MODE_ALWAYSON		1
#define   CXL_HMU_CFG2_EPOCH_LEN_MSK			GENMASK_ULL(63, 48)
#define CXL_HMU_CFG3_REG		0x60
#define   CXL_HMU_CFG3_HOTLIST_NOTI_THRESHOLD_MSK		GENMASK_ULL(15, 0)

#define CXL_HMU_STAT_REG		0x70
#define   CXL_HMU_STAT_STATUS_MSK				GENMASK_ULL(15, 0)
#define   CXL_HMU_STAT_OP_IN_PROGRESS_MSK		GENMASK_ULL(31, 16)
#define     CXL_HMU_STAT_OP_IN_PROGRESS_NONE		0
#define     CXL_HMU_STAT_OP_IN_PROGRESS_ENABLE		1
#define     CXL_HMU_STAT_OP_IN_PROGRESS_DISABLE		2
#define     CXL_HMU_STAT_OP_IN_PROGRESS_RESET		3
#define   CXL_HMU_STAT_COUNTER_WIDTH_MSK		GENMASK_ULL(39, 32)
#define   CXL_HMU_STAT_OVERFLOW_INT_MSK			GENMASK_ULL(47, 40)
#define     CXL_HMU_STAT_HOTLIST_OVERFLOW			BIT(0)
#define     CXL_HMU_STAT_LEVEL_CROSSED				BIT(1)

#define CXL_HMU_HOTLIST_HEAD_TAIL_REG	0x78
#define   CXL_HMU_HOTLIST_HEAD_MSK				GENMASK_ULL(15, 0)
#define   CXL_HMU_HOTLIST_TAIL_MSK				GENMASK_ULL(31, 16)

#define CXL_HMU_INSTANCE_OFFSET(info, i)	\
	(i * info->num_chmus * info->instance_len)

typedef enum {
	TIME_SCALE_MIN		= 1,
	TIME_SCALE_100US	= TIME_SCALE_MIN,
	TIME_SCALE_1MS		= 2,
	TIME_SCALE_10MS		= 3,
	TIME_SCALE_100MS	= 4,
	TIME_SCALE_1S		= 5,
	TIME_SCALE_MAX = TIME_SCALE_1S,
} TIME_SCALE;

enum cxl_hmu_type {
	CXL_HMU_MEMDEV,
};

struct cxl_hmu {
	struct device dev;
	void __iomem *base;
	int assoc_id;
	int index;
	enum cxl_hmu_type type;
};

struct cxl_hmu_status {
	u16 status;
	u16 op_in_progress;
	u8 counter_width;
	u8 overflow_itr_status;
};

struct cxl_hmu_config {
	u8 tracked_m2s_req;
	u8 flags;
	u16 control;
	u32 hotness_threshold;
	u32 unit_size;
	u8 down_sampling_factor;
	u8 reporting_mode;
	u16 epoch_len;
	u16 hotlist_noti_threshold;
};

#define CXL_HMU_MAX_CNT		8
struct cxl_hmu_info {
	struct device *dev;
	void __iomem *base;
	int version;
	u8 num_chmus;
	u16 instance_len;
	struct {
		int irq;
		bool hotlist_overflow_sup;
		bool hotlist_levels_crossing_sup;
		int epoch_type;
		u8 tracked_m2s_req;
		u16 max_epoch_len; /* in usec */
		u16 min_epoch_len; /* in usec */
		u16 hotlist_size;
		u32 supported_unit_size;
		u16 supported_down_sampling_factor;
		u16 flags;
		u64 range_conf_bitmap_reg; /* Offset where the CHMU Range Configuration
									  Bitmap starts */
		u64 hotlist_reg; /* Offset where the CHMU Hotlist starts */
	} caps[CXL_HMU_MAX_CNT];
	struct cxl_hmu_config config[CXL_HMU_MAX_CNT];
	struct cxl_hmu_status status[CXL_HMU_MAX_CNT];
	u64 hot_buf[1024];
	u16 hot_buf_head;
	u16 hot_buf_tail;
	spinlock_t hot_buf_lock;
};

#define to_cxl_hmu(dev) container_of(dev, struct cxl_hmu, dev)
struct cxl_hmu_regs;
int devm_cxl_hmu_add(struct device *parent, struct cxl_hmu_regs *regs,
		     int assoc_id, int idx, enum cxl_hmu_type type);
#endif
