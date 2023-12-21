#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <numa.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <sys/wait.h>
#include "common.h"
#include "util.h"
#include "threshold.h"
#include <numa.h>
#include <numaif.h>

static int threshold_child_pid;

bool threshold_exit;

char mlc_cmd[MAX_CMD_LEN];

int kill_threshold_child()
{
	int ret = 0;

	if (threshold_child_pid != 0) {
		ret = kill(threshold_child_pid, SIGKILL);
		if (ret == -1) {
			tierd_error("Fail to kill: %s\n", strerror(errno));
		} else {
			tierd_info("Stop threshold child process: %d \n", threshold_child_pid);
			threshold_child_pid = 0;
		}
	}

	return ret;
}

static inline int write_to_bandwidth_file(int nid, float bw, const char *name)
{
	for (int i = 0; i < bandwidth_f_num; i++) {
		if (!strcmp(name, bandwidth_f[i])) {
			char file_path[MAX_PATH_LEN];
			sprintf(file_path, "%s/%s", node_dir_path[nid], name);

			FILE *fd = fopen(file_path, "w");
			if (fd == NULL) {
				tierd_error("Fail to open %s\n", file_path);
				return -1;
			}

			fprintf(fd, "%.2f\n", bw);

			fclose(fd);
			break;
		}
	}

	return 0;
}

static inline int update_node_bandwidth_file(int nid)
{
	for (enum bandwidth_type type = BW_READ; type < MAX_NR_BW_TYPE; type++) {
		if(write_to_bandwidth_file(nid, max_bw[nid][type], bandwidth_f[type]))
			return -1;
	}

	return 0;
}

static inline int update_bandwidth_file()
{
	for (int nid = 0; nid < total_nodes; nid++)
		if (update_node_bandwidth_file(nid))
			return -1;

	return 0;
}

static inline void measure_body()
{
	pid_t pid = fork();
	if (pid < 0) {
		tierd_error("Fork Fail: %s\n", strerror(errno));
		return;
	} else if (pid == 0) {
		close(STDOUT_FILENO);
		/*
		 * "mlc --max_bandwidth" doesn't use memory-only node automatically.
		 * So we need to set interleave policy to use whole system's nodes.
		 * If fail to set memory policy, just run "mlc --bandwidth_matrix".
		 */
		unsigned long maxnode = total_nodes + 1;
		unsigned long nodemask = (1 << total_nodes) - 1;
		long ret = set_mempolicy(MPOL_INTERLEAVE, &nodemask, maxnode);
		if (ret != -1)
			execl(mlc_cmd, "mlc", "--max_bandwidth", NULL);
		else
			execl(mlc_cmd, "mlc", "--bandwidth_matrix", NULL);
	} else {
		int status;
		struct timespec begin, end;
		threshold_child_pid = pid;

		tierd_info("BW map generation starts ...\n");

		for (int nid = 0; nid < total_nodes; nid++) {
			max_bw[nid][BW_READ] = 0.0f;
			max_bw[nid][BW_WRITE] = 0.0f;
			max_bw[nid][BW_RDWR] = 0.0f;
		}
		clock_gettime(CLOCK_MONOTONIC, &begin);
		while(1) {
			if (unlikely(cleanup_start)) {
				kill_threshold_child();
				break;
			}

			pid_t result = waitpid(pid, &status, WNOHANG);
			if (result == -1)
				break;
			if (result == 0) {
				for (int nid = 0; nid < total_nodes; nid++) {
					float read_bw = cur_bw[head][nid][BW_READ];
					float write_bw = cur_bw[head][nid][BW_WRITE];

					clock_gettime(CLOCK_MONOTONIC, &end);
					tierd_debug("[%.2lf] Node %d : [%.2lf %.2lf]\n",
							GETTIME(begin, end), nid, read_bw, write_bw);
					max_bw[nid][BW_READ] = MAX(max_bw[nid][BW_READ], read_bw);
					max_bw[nid][BW_WRITE] = MAX(max_bw[nid][BW_WRITE], write_bw);
					max_bw[nid][BW_RDWR] = MAX(max_bw[nid][BW_RDWR],
							read_bw + write_bw);
				}

				usleep(interval_in_us);
			} else {
				clock_gettime(CLOCK_MONOTONIC, &end);
				tierd_info("Elapsed %.2lfs\n", GETTIME(begin, end));
				for (int nid = 0; nid < total_nodes; nid++) {
					tierd_info("Node %d : [%.2lf %.2lf %.2lf]\n",
							nid, max_bw[nid][BW_READ], max_bw[nid][BW_WRITE],
							max_bw[nid][BW_RDWR]);
					// Set Spike double of max_bw
					if (!stop_event)
						spike[nid] = MAX(spike[nid],
								(max_bw[nid][BW_RDWR] * 2.0f));
				}

				if (update_bandwidth_file()) {
					tierd_info("Update Bandwidth File Fail.\n");
					tierd_info("System may have no free memory.\n");
				}

				if (update_fallback_list()) {
					tierd_info("Update Fallback List Fail.\n");
					tierd_info("System may have no free memory.\n");
				}

				threshold_child_pid = 0;
				break;
			}
		}
	}
}

static inline void measure(bool init)
{
	pthread_mutex_lock(&mutex);

	while (!init && !cleanup_start && threshold_fin)
		pthread_cond_wait(&cond, &mutex);

	if (!cleanup_start)
		measure_body(mlc_cmd);

	threshold_fin = true;
	pthread_mutex_unlock(&mutex);
}

void *threshold_func(void *args __attribute__((unused)))
{
	tierd_info("Run threshold thread\n");

	snprintf(mlc_cmd, sizeof(mlc_cmd), "%s", mlc_file_path);

	for (int nid = 0; nid < total_nodes; nid++)
		spike[nid] = SPIKE;

	measure(1);

	pthread_mutex_lock(&sync_mutex);
	threshold_created = true;
	pthread_cond_signal(&sync_cond);
	pthread_mutex_unlock(&sync_mutex);

	while(1) {
		if (unlikely(cleanup_start))
			break;
		measure(0);
	}

	pthread_mutex_lock(&mutex);
	threshold_exit = true;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);

	pthread_exit(NULL);
}
