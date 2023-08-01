#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <numa.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include "common.h"
#include "util.h"
#include "policy.h"
#include "threshold.h"

#define THRESHOLD 90.0f // in %

static float threshold = THRESHOLD;

static int bottleneck_history[MAX_WINDOW][NUMA_NUM_NODES];

static inline bool check_node_bottleneck(int nid)
{
	for (int t = MAX_NR_BW_TYPE - 1; t >= 0; t--)
		if (cur_bw[head][nid][t] > ((threshold / 100.0f) * max_bw[nid][t]))
			return true;

	return false;
}

void *policy_func(void *args __attribute__((unused)))
{
	bwd_info("Run Policy Thread\n");

	int nid;

	while (1) {
		if (unlikely(cleanup_start))
			break;

		if (!threshold_fin) {
			usleep(interval);
			continue;
		}

		for (nid = 0; nid < total_nodes; nid++) {
			bool is_bottleneck = check_node_bottleneck(nid);
			int before = (head + max_window - 1) % max_window;

			bottleneck_history[head][nid] = is_bottleneck;
			if (bottleneck_history[before][nid] == bottleneck_history[head][nid])
				continue;

			FILE *fd = fopen(output_file_names[nid], "w");
			if (fd == NULL) {
				bwd_error("Failed to open output file %s: %s\n",
							output_file_names[nid], strerror(errno));
				pthread_exit(NULL);
			}

			fprintf(fd, is_bottleneck ? "true" : "false");
			fclose(fd);
		}

		usleep(interval);
	}

	pthread_exit(NULL);
}
