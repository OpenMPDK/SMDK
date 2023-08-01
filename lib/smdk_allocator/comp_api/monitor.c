/*
   Copyright, Samsung Corporation

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in
 the documentation and/or other materials provided with the
 distribution.

 * Neither the name of the copyright holder nor the names of its
 contributors may be used to endorse or promote products derived
 from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "internal/init.h"
#include "internal/alloc.h"
#include "internal/config.h"
#include "internal/monitor.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <numa.h>
#include <sys/inotify.h>

#define MAX_PATH_LEN    256
#define MAX_CMD_LEN     (MAX_PATH_LEN + 32)

#define BWD_DIR		"/run/bwd/"

#define MAX_EVENTS		10
#define EVENT_SIZE		(sizeof(struct inotify_event))
#define BUF_SIZE		(MAX_EVENTS * (EVENT_SIZE + 16))

#define for_each_node(node, total_nodes)	\
	for (node = 0; node < total_nodes; node++)

static char policy_file_names[NUMA_NUM_NODES][MAX_PATH_LEN];

static int total_nodes;

static pthread_t monitor_thread;

static inline void update_smdk_current_prio(int val)
{
	pthread_rwlock_wrlock(&smdk_info.rwlock_current_prio);
	smdk_info.current_prio = val;
	pthread_rwlock_unlock(&smdk_info.rwlock_current_prio);
}

static int do_adaptive_interleave(int nid, bool *is_bottleneck)
{
	int fd;
	char buf[BUF_SIZE] = { '\0', };
	size_t nread;

	if (nid < 0 || !is_bottleneck)
		return -1;

	fd = open(policy_file_names[nid], O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Failed to open file %s: %s\n", policy_file_names[nid],
				strerror(errno));
		return -1;
	}

	nread = read(fd, buf, BUF_SIZE);
	if (nread == -1) {
		fprintf(stderr, "Failed to read file %s: %s\n", policy_file_names[nid],
				strerror(errno));
		close(fd);
		return -1;
	}

	if (!strncmp(buf, "true", strlen("true"))) {
		fprintf(stderr, "adaptive interleaving: CXL DRAM\n");
		update_smdk_current_prio(1);
		*is_bottleneck = true;
	} else if (!strncmp(buf, "false", strlen("false"))) {
		fprintf(stderr, "adaptive interleaving: DDR DRAM\n");
		update_smdk_current_prio(0);
		*is_bottleneck = false;
	}

	close(fd);
	return 0;
}

static void *monitor_func(void *args __attribute__((unused)))
{
	int nid, fd, wd[NUMA_NUM_NODES];
	int len;
	char buf[BUF_SIZE], *ptr;
	struct inotify_event *event;
	bool is_bottleneck = false;

	for (int i = 0 ; i < total_nodes; i++)
		wd[i] = -1;

	fd = inotify_init();
	if (fd == -1) {
		fprintf(stderr, "Failed to init inotify: %s\n", strerror(errno));
		goto exit;
	}

	for_each_node(nid, total_nodes) {
		if (do_adaptive_interleave(nid, &is_bottleneck))
			goto exit;

		if (is_bottleneck)
			break;
	}

	for_each_node(nid, total_nodes) {
		wd[nid] = inotify_add_watch(fd, policy_file_names[nid], IN_MODIFY);
		if (wd[nid] == -1) {
			fprintf(stderr, "Failed to inotify add watch: %s: %s\n",
					policy_file_names[nid], strerror(errno));
			goto exit;
		}

	}

	while(1) {
		len = read(fd, buf, BUF_SIZE);
		if (len < 0) {
			fprintf(stderr, "Failed to read inotify fd\n");
			goto exit;
		}

		for (ptr = buf; ptr < buf + len;
				ptr += sizeof(struct inotify_event) + event->len) {
			event = (struct inotify_event *)ptr;

			if (event->mask & IN_MODIFY) {
				for_each_node(nid, total_nodes) {
					if (event->wd != wd[nid])
						continue;

					if (do_adaptive_interleave(nid, &is_bottleneck))
						goto exit;
				}
			}
		}
	}

exit:
	if (fd != -1) {
		for_each_node(nid, total_nodes) {
			if (wd[nid] != -1)
				inotify_rm_watch(fd, wd[nid]);
		}

		close(fd);
	}

	pthread_exit(NULL);
}

inline static int check_policy_directory(void)
{
	struct stat st;

	if (stat(BWD_DIR, &st) != 0) {
		fprintf(stderr, "policy directory does not exist (%s): %s\n", BWD_DIR,
				strerror(errno));
		return -1;
	}

	return 0;
}

inline static int set_policy_file_names(void)
{
	for (int i = 0; i < total_nodes; i++) {
		snprintf(policy_file_names[i], sizeof(policy_file_names[i]),
				"%s/node%d", BWD_DIR, i);
	}

	return 0;
}

int init_monitor_thread(void)
{
	int ret;

	if (!opt_smdk.use_adaptive_interleaving)
		return -1;

	if (opt_smdk.adaptive_interleaving_policy != policy_bw_saturation) {
		fprintf(stderr, "invalid adaptive_interleaving_policy: %d\n",
				opt_smdk.adaptive_interleaving_policy);
		goto fail;
	}

	total_nodes = numa_max_node() + 1;

	ret = check_policy_directory();
	if (ret != 0)
		goto fail;

	ret = set_policy_file_names();
	if (ret != 0) {
		fprintf(stderr, "Failed to set policy file names\n");
		goto fail;
	}

	ret = pthread_create(&monitor_thread, NULL, monitor_func, NULL);
	if (ret != 0) {
		fprintf(stderr, "Failed to create monitor thread: %s\n", strerror(ret));
		goto fail;
	}

	return 0;

fail:
	fprintf(stderr, "*** use_adaptive_interleaving is disabled\n");
	return -1;
}
