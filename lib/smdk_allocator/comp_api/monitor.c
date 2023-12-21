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

#include <numa.h>
#include <urcu.h>
#include <numaif.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/syscall.h>

#define NODE_DIR_LEN    2048
#define MAX_PATH_LEN    4096

#define MAX_THREADS NUMA_NUM_NODES

#define MAX_EVENTS		10
#define EVENT_SIZE		(sizeof(struct inotify_event))
#define BUF_SIZE		(MAX_EVENTS * (EVENT_SIZE + 16))

#define TIERD_ROOT_DIR	"/run/tierd/"
#define IDLE_STATE_BW_FILE "idle_state_bandwidth"
#define IDLE_STATE_CAPA_FILE "idle_state_capacity"
#define MAX_READ_BW_FILE "max_read_bw"
#define MAX_WRITE_BW_FILE "max_write_bw"
#define MAX_BW_FILE "max_bw"
#define FALLBACK_ORDER_BW_FILE "fallback_order_bandwidth"
#define FALLBACK_ORDER_TIER_FILE "fallback_order_tier"
#define MAX_STATE_FILES 1024
#define MPOL_WEIGHTED_INTERLEAVE MPOL_DEFAULT + 8

bool weighted_interleaving = false;

static int policy_idx;
static int total_nodes;
static int total_sockets;
static int state_file_type[MAX_STATE_FILES];

static bool monitor_init = false;

static char node_dir_path[NUMA_NUM_NODES][NODE_DIR_LEN];

static unsigned char *weights;

static struct bitmask *wil_nodes;
static struct bitmask *origin_wil_nodes;

static struct mempolicy_args wil_args;

static pthread_t foreground_tid;
static pthread_t monitor_thread[MAX_THREADS];
static pthread_cond_t monitor_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t monitor_mutex = PTHREAD_MUTEX_INITIALIZER;

// NUM_OF_SOCKET x NUM_OF_RELATED_FILES
static bool **g_idle_state;
static float **g_max_bw;
// NUM_OF_SOCKET x NUM_OF_RELATED_FILES x NUM_OF_NODES
static int ***g_fallback_order;

static int idle_state_f_num;
static char idle_state_f[][64] = {
	IDLE_STATE_BW_FILE,
	IDLE_STATE_CAPA_FILE,
};
enum idle_state_f_enum {
	IDLE_STATE_BW = 0,
	IDLE_STATE_CAPA,
};

static int max_bw_f_num;
static char max_bw_f[][64] = {
	MAX_READ_BW_FILE,
	MAX_WRITE_BW_FILE,
	MAX_BW_FILE,
};
enum max_bw_f_enum {
	MAX_READ_BW = 0,
	MAX_WRITE_BW,
	MAX_RDWR_BW,
};

static int fallback_order_f_num;
// Order of fallback_order_file must match with include/internal/config.h
static char fallback_order_f[][64] = {
	FALLBACK_ORDER_TIER_FILE, // For policy_bw_saturation
	FALLBACK_ORDER_BW_FILE // For policy_bw_order
};

enum file_type {
	IDLE_STATE = 1,
	MAX_BW,
	FALLBACK_ORDER,
	FILE_TYPE_MAX,
};

static int set_state_files(int target_nid, char (*managed_files)[MAX_PATH_LEN])
{
	int i, rc, total_managed_files = 0;
	char file_buf[MAX_PATH_LEN];
	for (i = 0; i < idle_state_f_num; i++) {
		rc = snprintf(file_buf, sizeof(file_buf),
				"%s/%s", node_dir_path[target_nid], idle_state_f[i]);
		state_file_type[total_managed_files] = IDLE_STATE;
		strncpy(managed_files[total_managed_files++], file_buf, rc);
	}
	for (i = 0; i < max_bw_f_num; i++) {
		rc = snprintf(file_buf, sizeof(file_buf),
				"%s/%s", node_dir_path[target_nid], max_bw_f[i]);
		state_file_type[total_managed_files] = MAX_BW;
		strncpy(managed_files[total_managed_files++], file_buf, rc);
	}
	if (target_nid < total_sockets)
		for (i = 0; i < fallback_order_f_num; i++) {
			rc = snprintf(file_buf, sizeof(file_buf),
					"%s/%s", node_dir_path[target_nid], fallback_order_f[i]);
			state_file_type[total_managed_files] = FALLBACK_ORDER;
			strncpy(managed_files[total_managed_files++], file_buf, rc);
		}

	return total_managed_files;
}

static void set_state_info(int nid, int idx, char *buf,
		bool *idle_state, float *max_bw, int **fallback_order)
{
	int rIdx;
	bool callback = false;

	switch (state_file_type[idx]) {
		case IDLE_STATE:
			callback = true;
			idle_state[idx] = strtobool(buf);
			g_idle_state[nid][idx] = idle_state[idx];
			break;
		case MAX_BW:
			callback = true;
			rIdx = idx - idle_state_f_num;
			max_bw[rIdx] = atof(buf);
			g_max_bw[nid][rIdx] = max_bw[rIdx];
			break;
		case FALLBACK_ORDER:
			if (weighted_interleaving)
				break;
			rIdx = idx - (idle_state_f_num + max_bw_f_num);
			int *new_ptr, *old_ptr;
			new_ptr = (int *)alloc_arr(total_nodes, sizeof(int));
			if (!new_ptr) {
				fprintf(stderr, "Failed to alloc fallback_order buf\n");
				pthread_exit(NULL);
			}
			parse_order_string(buf, new_ptr, total_nodes);
			old_ptr = rcu_xchg_pointer(&g_fallback_order[nid][rIdx], new_ptr);
			synchronize_rcu();
			free(old_ptr);
			break;
		default:
			fprintf(stderr, "Unknown state_files\n");
	}

	if (weighted_interleaving && callback && monitor_init)
		pthread_kill(foreground_tid, SIGUSR1);
}

static void read_state_file(int nid, int idx,
		const char (*managed_files)[MAX_PATH_LEN],
		bool *idle_state, float *max_bw, int **fallback_order)
{
	int fd;
	char buf[BUF_SIZE] = { '\0', };
	size_t rc;

	fd = open(managed_files[idx], O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Failed to open %s: %s\n",
				managed_files[idx], strerror(errno));
		return;
	}
	rc = read(fd, buf, BUF_SIZE);
	if (rc < 0) {
		fprintf(stderr, "Failed to read %s: %s\n",
				managed_files[idx], strerror(errno));
		close(fd);
		return;
	}
	close(fd);

	set_state_info(nid, idx, buf, idle_state, max_bw, fallback_order);
}

static inline int init_state_file(void)
{
	int nid;

	if (access(TIERD_ROOT_DIR, F_OK) != 0) {
		fprintf(stderr, "%s is not exist: %s\n", TIERD_ROOT_DIR,
				strerror(errno));
		return -1;
	}

	for (nid = 0; nid < total_nodes; nid++) {
		snprintf(node_dir_path[nid], sizeof(node_dir_path[nid]),
				"%s/node%d", TIERD_ROOT_DIR, nid);
		if (access(node_dir_path[nid], F_OK) != 0) {
			fprintf(stderr, "%s is not exist: %s\n",
					node_dir_path[nid], strerror(errno));
			return -1;
		}
	}

	idle_state_f_num = (int)(sizeof(idle_state_f) / sizeof(idle_state_f[0]));
	max_bw_f_num = (int)(sizeof(max_bw_f) / sizeof(max_bw_f[0]));
	fallback_order_f_num = (int)(sizeof(fallback_order_f) /
			sizeof(fallback_order_f[0]));

	return 0;
}

static inline bool is_node_state_idle(int n)
{
	return g_idle_state[n][IDLE_STATE_BW] && g_idle_state[n][IDLE_STATE_CAPA];
}

int get_best_pool_id(int socket_id)
{
	if (!monitor_init)
		return socket_id;

	if (total_nodes <= 0 || total_sockets <= 0)
		return socket_id;

	int i, candidate, best = -1;
	int *local_fallback_order;

	rcu_register_thread();

	rcu_read_lock();

	local_fallback_order =
		rcu_dereference(g_fallback_order[socket_id][policy_idx]);

	for (i = 0; i < total_nodes; i++) {
		candidate = local_fallback_order[i];
		/* Handle the case when fallback order isn't ready yet. */
		if (candidate == -1) {
			best = socket_id;
			break;
		}
		if (!is_node_state_idle(candidate))
			continue;
		best = candidate;
		break;
	}

	rcu_read_unlock();

	rcu_unregister_thread();

	return best;
}

static struct bitmask *alloc_wil_bitmask()
{
	if (opt_smdk.interleave_node[0] == '\0') {
		origin_wil_nodes = numa_allocate_nodemask();
		if (!origin_wil_nodes)
			return NULL;

		wil_nodes = numa_allocate_nodemask();
		if (!wil_nodes)
			return NULL;

		for (int i = 0; i < total_nodes; i++) {
			numa_bitmask_setbit(wil_nodes, i);
			numa_bitmask_setbit(origin_wil_nodes, i);
		}
		fprintf(stderr, "Nodelist: 0-%d\n", total_nodes-1);
	} else {
		origin_wil_nodes = numa_parse_nodestring(opt_smdk.interleave_node);
		if (!origin_wil_nodes)
			return NULL;

		wil_nodes = numa_parse_nodestring(opt_smdk.interleave_node);
		if (!wil_nodes)
			return NULL;

		fprintf(stderr, "Nodelist: %s\n", opt_smdk.interleave_node);
	}

	return wil_nodes;
}

static void set_wil_args()
{
	wil_args.maxnode = total_nodes + 1;
	wil_args.wil.weights = weights;
	wil_args.nodemask = wil_nodes->maskp;
	wil_args.mode = MPOL_WEIGHTED_INTERLEAVE;
	wil_args.flags = 0;
}

static void set_wil_weights()
{
	float sum = 0.0f;
	bool is_calcuated[total_nodes];

	for (int nid = 0; nid < total_nodes; nid++) {
		weights[nid] = 0;
		is_calcuated[nid] = false;
		numa_bitmask_clearbit(wil_nodes, nid);
		if (!numa_bitmask_isbitset(origin_wil_nodes, nid))
			continue;
		if (!is_node_state_idle(nid))
			continue;
		if (g_max_bw[nid][MAX_RDWR_BW] < EPS)
			continue;
		is_calcuated[nid] = true;
		sum += g_max_bw[nid][MAX_RDWR_BW];
	}

	if (sum < EPS) {
		fprintf(stderr, "Set all Interleave node's weights equally.\n");
		for (int nid = 0; nid < total_nodes; nid++) {
			if (!numa_bitmask_isbitset(origin_wil_nodes, nid))
				continue;
			if (!is_node_state_idle(nid))
				continue;
			weights[nid] = 1;
			numa_bitmask_setbit(wil_nodes, nid);
		}
	} else {
		for (int nid = 0; nid < total_nodes; nid++) {
			if (!is_calcuated[nid])
				continue;
			weights[nid] = (unsigned char)round(g_max_bw[nid][MAX_RDWR_BW]
					* (100.0f / sum));
			numa_bitmask_setbit(wil_nodes, nid);
		}
	}
}

static int call_wil_syscall()
{
	return SET_MEMPOLICY2(&wil_args, sizeof(wil_args));
}

static int do_weighted_interleaving()
{
	set_wil_weights();

	set_wil_args();

	if (call_wil_syscall()) {
		fprintf(stderr, "Failed to set_mempolicy: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static void wil_callback(int signo) {
	switch (signo) {
		case SIGUSR1:
			(void)do_weighted_interleaving();
			break;
		default:
			break;
	}
}

static void *monitor_func(void *args)
{
	int i, fd, len;
	int total_managed_files;
	int target_nid = *(int *)args;
	int wd[NUMA_NUM_NODES] = { [0 ... NUMA_NUM_NODES -1] = -1 };

	char *ptr;
	char buf[BUF_SIZE];
	char managed_files[MAX_STATE_FILES][MAX_PATH_LEN];

	bool idle_state[idle_state_f_num];
	float max_bw[max_bw_f_num];
	int **fallback_order = NULL;

	struct inotify_event *event;

	if (target_nid < total_sockets) {
		fallback_order = (int **)alloc_arr_2d(fallback_order_f_num,
				total_nodes, sizeof(int));
		if (!fallback_order) {
			fprintf(stderr, "Failed to alloc fallback_order buf\n");
			goto exit;
		}
	}

	total_managed_files = set_state_files(target_nid, managed_files);

	for (i = 0; i < total_managed_files; i++)
		read_state_file(target_nid, i, managed_files,
				idle_state, max_bw, fallback_order);

	pthread_mutex_lock(&monitor_mutex);
	pthread_cond_signal(&monitor_cond);
	pthread_mutex_unlock(&monitor_mutex);

	fd = inotify_init();
	if (fd == -1) {
		fprintf(stderr, "Failed to init inotify: %s\n", strerror(errno));
		goto exit;
	}

	for (i = 0; i < total_managed_files; i++) {
		wd[i] = inotify_add_watch(fd, managed_files[i], IN_MODIFY);
		if (wd[i] == -1) {
			fprintf(stderr, "Failed to inotify add watch (%s): %s\n",
					managed_files[i], strerror(errno));
			goto exit;
		}
	}

	while (1) {
		len = read(fd, buf, BUF_SIZE);
		if (len < 0) {
			fprintf(stderr, "Failed to read inotify fd\n");
			goto exit;
		}

		for (ptr = buf; ptr < buf + len;
				ptr += sizeof(struct inotify_event) + event->len) {
			event = (struct inotify_event *)ptr;

			if (event->mask & IN_MODIFY) {
				for (i = 0; i < total_managed_files; i++) {
					if (event->wd != wd[i])
						continue;

					read_state_file(target_nid, i, managed_files,
							idle_state, max_bw, fallback_order);
				}
			}
		}
	}

exit:
	if (fd != -1) {
		for (i = 0; i < total_managed_files; i++) {
			if (wd[i] != -1)
				inotify_rm_watch(fd, wd[i]);
		}
		close(fd);
	}

	free_arr_2d((void **)fallback_order, fallback_order_f_num);

	pthread_exit(NULL);
}

static int wakeup_monitor_thread()
{
	for (int nid = 0; nid < total_nodes; nid++) {
		pthread_mutex_lock(&monitor_mutex);
		int ret = pthread_create(&monitor_thread[nid],
				NULL, monitor_func, (void*)&nid);
		if (ret != 0) {
			fprintf(stderr, "Failed to create monitor thread(%d): %s\n",
					nid, strerror(ret));
			pthread_mutex_unlock(&monitor_mutex);
			return -1;
		}

		pthread_cond_wait(&monitor_cond, &monitor_mutex);
		pthread_mutex_unlock(&monitor_mutex);
	}

	return 0;
}

int init_monitor_thread(void)
{
	int ret;

	if (monitor_init)
		return 0;

	if (!opt_smdk.use_adaptive_interleaving)
		goto fail;

	if (opt_smdk.adaptive_interleaving_policy != policy_bw_saturation &&
		opt_smdk.adaptive_interleaving_policy != policy_bw_order &&
		opt_smdk.adaptive_interleaving_policy != policy_weighted_interleaving) {
		fprintf(stderr, "invalid adaptive_interleaving_policy: %d\n",
				opt_smdk.adaptive_interleaving_policy);
		goto fail;
	}

	weighted_interleaving = opt_smdk.adaptive_interleaving_policy ==
		policy_weighted_interleaving;

	total_sockets = get_total_sockets();
	if (total_sockets <= 0) {
		fprintf(stderr, "Fail to get total sockets\n");
		goto fail;
	}

	total_nodes = numa_max_node() + 1;
	if (total_nodes <= 0) {
		fprintf(stderr, "Failed to get total nodes\n");
		goto fail;
	}

	ret = init_state_file();
	if (ret != 0) {
		fprintf(stderr, "Failed to init state files\n");
		goto fail;
	}

	g_idle_state = (bool **)alloc_arr_2d(total_nodes, idle_state_f_num,
			sizeof(bool));
	if (!g_idle_state) {
		fprintf(stderr, "Failed to alloc idle_state buffer\n");
		goto fail;
	}

	g_max_bw = (float **)alloc_arr_2d(total_nodes, max_bw_f_num,
			sizeof(float));
	if (!g_max_bw) {
		fprintf(stderr, "Failed to alloc max_bw buffer\n");
		goto fail;
	}

	g_fallback_order = (int ***)alloc_arr_3d(total_sockets,
			fallback_order_f_num, total_nodes, sizeof(int));
	if (!g_fallback_order) {
		fprintf(stderr, "Failed to alloc fallback_order buffer\n");
		goto fail;
	}

	policy_idx = weighted_interleaving ? 0 :
		opt_smdk.adaptive_interleaving_policy;
	if (wakeup_monitor_thread())
		goto fail;

	if (weighted_interleaving) {
		foreground_tid = pthread_self();

		wil_nodes = alloc_wil_bitmask();
		if (!wil_nodes) {
			fprintf(stderr, "Failed to alloc wil bitmask\n");
			goto fail;
		}

		weights = (unsigned char *)alloc_arr(total_nodes, sizeof(unsigned char));
		if (!weights) {
			fprintf(stderr, "Failed to alloc weights buf\n");
			goto fail;
		}

		if (do_weighted_interleaving()) {
			fprintf(stderr, "Failed to set weighted interleaving policy\n");
			goto fail;
		}

		// Register Callback for Weighted Interleaving.
		signal(SIGUSR1, wil_callback);
	}

	monitor_init = true;

	return 0;

fail:
	fprintf(stderr, "*** use_adaptive_interleaving is disabled\n");
	return -1;
}
