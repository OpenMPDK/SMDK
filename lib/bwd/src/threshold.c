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

static int threshold_child_pid;

bool threshold_exit;

char mlc_cmd[MAX_CMD_LEN];

int kill_threshold_child()
{
	int ret = 0;

	if (threshold_child_pid != 0) {
		ret = kill(threshold_child_pid, SIGKILL);
		if (ret == -1) {
			bwd_error("Fail to kill: %s\n", strerror(errno));
		} else {
			bwd_info("Stop threshold child process : %d \n", threshold_child_pid);
			threshold_child_pid = 0;
		}
	}

	return ret;
}

static inline void measure_body()
{
	pid_t pid = fork();
	if (pid < 0) {
		bwd_error("Fork Fail: %s\n", strerror(errno));
		return;
	} else if (pid == 0) {
		close(STDOUT_FILENO);
		execl(mlc_cmd, "mlc", "--max_bandwidth", NULL);
	} else {
		int status;
		struct timespec begin, end;
		threshold_child_pid = pid;

		bwd_info("BW map generation starts ...\n");

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
					bwd_debug("[%.2lf] Node %d : [%.2lf %.2lf]\n",
							GETTIME(begin, end), nid, read_bw, write_bw);
					max_bw[nid][BW_READ] = MAX(max_bw[nid][BW_READ], read_bw);
					max_bw[nid][BW_WRITE] = MAX(max_bw[nid][BW_WRITE], write_bw);
					max_bw[nid][BW_RDWR] = MAX(max_bw[nid][BW_RDWR],
												read_bw + write_bw);
				}

				usleep(interval);
			} else {
				clock_gettime(CLOCK_MONOTONIC, &end);
				bwd_info("Elapsed %.2lfs\n", GETTIME(begin, end));
				for (int nid = 0; nid < total_nodes; nid++) {
					bwd_info("Node %d : [%.2lf %.2lf %.2lf]\n",
							nid, max_bw[nid][BW_READ], max_bw[nid][BW_WRITE],
							max_bw[nid][BW_RDWR]);
					// Set Spike double of max_bw
					if (!stop_event)
						spike[nid] = MAX(spike[nid],
										(max_bw[nid][BW_RDWR] * 2.0f));
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
	bwd_info("Run threshold thread\n");

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
