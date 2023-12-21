/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Samsung LTD. All rights reserved. */

#ifndef __SLS_PARAMETERS_H__
#define __SLS_PARAMETERS_H__

#include <linux/sls_common.h>
#include <linux/types.h>

struct sls_param {
	int64_t value;
	bool is_set;
};

struct sls_params {
	struct sls_param dev_type;
	struct sls_param nr_cunits;
	struct sls_param aligned_tag_sz; // Single TAG size (Bytes)
	struct sls_param data_sz;
	struct sls_param buf_sz; // Instruction, Psum & Tags Buffers Size (Bytes)
	struct sls_param nr_inst_buf;
	struct sls_param nr_psum_buf;
	struct sls_param nr_tag_buf;
	struct sls_param reg_en;
	struct sls_param reg_exec;
	struct sls_param reg_poll;
	struct sls_param mem_size_gb;
	struct sls_param base_addr;
};

#define NR_CUNITS_PER_CS 2
#define NR_CS_PER_CH 2

struct sls_topology {
	int dev_type;
	int nr_cunits;
	int nr_ranks;
	int nr_cs;
	int nr_ch;
	int aligned_tag_sz; /* Single TAG size (Bytes) */
	int inst_sz;
	int data_sz;
	int buf_sz; /* Instruction, Psum & Tags Buffers Size (Bytes) */
	int nr_inst_buf;
	int nr_psum_buf;
	int nr_tag_buf;
	int reg_en;
	int reg_exec;
	int reg_poll;
	int alignment_sz;
	int mem_size_gb;
	uint64_t block_sz[SLS_BLOCK_MAX];
	uint64_t base_addr;
};

const struct sls_topology *sls_topo(void);

int init_topology(void);

#endif // __SLS_PARAMETERS_H__
