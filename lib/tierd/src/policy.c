#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <numa.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "util.h"
#include "policy.h"
#include "threshold.h"

// TODO - Can we dynamically change these??
// TODO - Set by get user param
#define BANDWIDTH_THRESHOLD 90.0f // in %
#define CAPACITY_THRESHOLD 90.0f // in %

enum policy_type {
	BANDWIDTH,
	CAPACITY,
	MAX_POLICY_TYPE,
};

static float bandwidth_threshold = BANDWIDTH_THRESHOLD;
static float capacity_threshold = CAPACITY_THRESHOLD;

static bool history[MAX_POLICY_TYPE][MAX_WINDOW][NUMA_NUM_NODES];
static bool written_state[MAX_POLICY_TYPE][NUMA_NUM_NODES];

static int window[MAX_POLICY_TYPE][NUMA_NUM_NODES];

/*
 * Note that we should to write true to the state file when saturation is false.
 */
static inline int write_to_state_file(int nid, bool saturation, const char *name)
{
	for (int i = 0; i < state_f_num; i++) {
		if (!strcmp(name, state_f[i])) {
			char file_path[MAX_PATH_LEN];
			sprintf(file_path, "%s/%s", node_dir_path[nid], name);

			FILE *fd = fopen(file_path, "w");
			if (fd == NULL) {
				tierd_error("Fail to open %s\n", file_path);
				return -1;
			}

			fprintf(fd, !saturation ? "true\n" : "false\n");

			fclose(fd);
			break;
		}
	}

	return 0;
}

static inline void increase_window(int nid, enum policy_type type)
{
	if (window[type][nid] < max_window)
		window[type][nid] <<= 1;
}

static inline void decrease_window(int nid, enum policy_type type)
{
	if (window[type][nid] > 1)
		window[type][nid] >>= 1;
}

static bool get_current_state(int nid, bool before, enum policy_type type)
{
	int current;
	int befores[max_window];

	for (int i = 0; i < window[type][nid]; i++)
		befores[i] = (max_window + head - i) % max_window;
	current = befores[0];

	if (window[type][nid] == 1) {
		increase_window(nid, type);
		if (history[type][current][nid])
			return 1;
		return 0;
	}

	for (int i = 1; i < window[type][nid]; i++) {
		int ind1 = befores[i - 1];
		int ind2 = befores[i];
		if (history[type][ind1][nid] != history[type][ind2][nid]) {
			decrease_window(nid, type);
			if (before)
				return 1;
			return 0;
		}
	}

	increase_window(nid, type);
	if (history[type][current][nid])
		return 1;
	return 0;
}

static void update_bandwidth_head(int nid)
{
	float percentage = bandwidth_threshold / 100.0f;
	for (int t = MAX_NR_BW_TYPE - 1; t >= 0; t--) {
		/* When Live CXL B/W is not measured. Just treat it as idle. */
		if (max_bw[nid][t] < EPS) {
			history[BANDWIDTH][head][nid] = 0;
			return;
		}
		if (cur_bw[head][nid][t] > percentage * max_bw[nid][t]) {
			history[BANDWIDTH][head][nid] = 1;
			return;
		}
	}

	history[BANDWIDTH][head][nid] = 0;
}

static void update_capacity_head(int nid)
{
	long long free_size_in_byte;
	long long total_size_in_byte = numa_node_size64(nid, &free_size_in_byte);
	double capacity_utilization;

	/*
	 * If we cannot read node meminfo, judge the node has sufficient capacity.
	 * It still works anyway.
	 */
	if (total_size_in_byte == -1L) {
		history[CAPACITY][head][nid] = 0;
		return;
	}

	capacity_utilization = 1.0f - (float)free_size_in_byte / total_size_in_byte;
	capacity_utilization *= 100.0f;
	if (capacity_utilization > capacity_threshold) {
		history[CAPACITY][head][nid] = 1;
		return;
	}

	history[CAPACITY][head][nid] = 0;
}

static void update_head(int nid, enum policy_type type)
{
	switch (type) {
		case BANDWIDTH:
			update_bandwidth_head(nid);
			break;
		case CAPACITY:
			update_capacity_head(nid);
			break;
		default:
			tierd_error("Unrecognized Policy\n");
			pthread_exit(NULL);
			break;
	}
}

static int update_state(int nid, enum policy_type type)
{
	bool current_state;
	bool written;

	if (written_state[type][nid])
		written = 1;
	else
		written = 0;

	update_head(nid, type);

	current_state = get_current_state(nid, written, type);
	if (current_state == written)
		return 0;

	if (write_to_state_file(nid, current_state, state_f[type]))
		return -1;

	if (current_state)
		written_state[type][nid] = 1;
	else
		written_state[type][nid] = 0;

	return 0;
}

static int update_node_state(int nid)
{
	for (enum policy_type type = BANDWIDTH; type < MAX_POLICY_TYPE; type++) {
		if (update_state(nid, type))
			return -1;
	}

	return 0;
}

static inline void initialize_meta()
{
	for (int i = 0; i < MAX_POLICY_TYPE; i++) {
		for (int j = 0; j < total_nodes; j++) {
			window[i][j] = 1;
			written_state[i][j] = 0;
		}
	}
}
void *policy_func(void *args __attribute__((unused)))
{
	tierd_info("Run Policy Thread\n");

	initialize_meta();

	while (1) {
		if (unlikely(cleanup_start))
			break;

		if (!threshold_fin) {
			usleep(interval_in_us);
			continue;
		}

		for (int nid = 0; nid < total_nodes; nid++) {
			if (update_node_state(nid)) {
				tierd_error("Fail to update state file for node %d\n", nid);
				pthread_exit(NULL);
			}
		}

		usleep(interval_in_us);
	}

	pthread_exit(NULL);
}
