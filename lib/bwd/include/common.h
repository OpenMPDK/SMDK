#ifndef __BWD_COMMON_H__
#define __BWD_COMMON_H__

#include "util.h"

extern float cur_bw[MAX_WINDOW][NUMA_NUM_NODES][MAX_NR_BW_TYPE];
extern float max_bw[NUMA_NUM_NODES][MAX_NR_BW_TYPE];

extern bool cleanup_start;

extern int total_nodes;
extern int interval;

#endif
