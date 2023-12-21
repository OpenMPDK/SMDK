#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <numa.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <libgen.h>
#include <dirent.h>
#include "daemon.h"
#include "util.h"

static pthread_t monitor_thread;
static pthread_t threshold_thread;
static pthread_t policy_thread;

static bool cleanup_fin = false;

static int daemonize = 0;

pthread_mutex_t cleanup_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cleanup_cond  = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
pthread_mutex_t sync_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  sync_cond  = PTHREAD_COND_INITIALIZER;

bool cleanup_start = false;
bool threshold_created = false;
bool threshold_fin = false;

float cur_bw[MAX_WINDOW][NUMA_NUM_NODES][MAX_NR_BW_TYPE];
float max_bw[NUMA_NUM_NODES][MAX_NR_BW_TYPE];

int spike[NUMA_NUM_NODES];
int total_sockets;
int total_nodes;
int max_window;
int threshold;
int interval_in_us;
int stop_event;

char mlc_file_path[MAX_PATH_LEN];
#ifdef ARCH_AMD
char amd_uprofpcm_file_path[MAX_PATH_LEN];
#endif
char *config_file_name = CONFIG_FILE_PATH;
char **node_dir_path;

/*
 * Files for node's saturation state.
 * Used for src/policy.c
 */
int state_f_num;
char state_f[][64] = {
	IDLE_STATE_BW_FILE,
	IDLE_STATE_CAPA_FILE,
};

/*
 * Files for saving node's max bandwidth for weighted interleaving.
 * Used for src/threshold.c
 */
int bandwidth_f_num;
char bandwidth_f[][64] = {
	MAX_READ_BW_FILE,
	MAX_WRITE_BW_FILE,
	MAX_BW_FILE,
};

/*
 * Files for node has CPU like DRAM(Don't need to Memory Only Node like CXL).
 * Used for src/distance.c
 */
int fallback_order_f_num;
char fallback_order_f[][64] = {
	FALLBACK_ORDER_BW_FILE,
	FALLBACK_ORDER_TIER_FILE
};

static void print_usage(void)
{
	fprintf(stderr,
			"Usage: tierd [-hD] [-c config_file] [-i interval_in_us(>= 1000000)] [-w window_size(>= 1)\n");
	exit(EXIT_FAILURE);
}

typedef enum {
	BAD_OPTION,
	MLC_PATH,
#ifdef ARCH_AMD
	AMD_UPROFPCM_PATH
#endif
} tierd_opcodes;

static struct {
	const char *name;
	tierd_opcodes opcode;
} config_keywords[] = {
	{ "MLC_PATH", MLC_PATH },
#ifdef ARCH_AMD
	{ "AMD_UPROFPCM_PATH", AMD_UPROFPCM_PATH },
#endif
	{ NULL, BAD_OPTION }
};

static tierd_opcodes parse_token(const char *cp)
{
	for (int i = 0; config_keywords[i].name; i++) {
		if (strcasecmp(cp, config_keywords[i].name) == 0) {
			return config_keywords[i].opcode;
		}
	}

	return BAD_OPTION;
}

static int parse_daemon_config(const char *conf_filename)
{
	FILE *conf_fp;
	char *filename;
	int ret = -1;

	conf_fp = fopen(conf_filename, "r");
	if (conf_fp == NULL) {
		tierd_error("Failed to open config file\n");
		return ret;
	}

	char line[256] = { '\0', };
	while(fgets(line, sizeof(line), conf_fp)) {
		char *result = strchr(line, '=');
		if (result == NULL)
			continue;

		char key[64] = { '\0', };
		int i = 0;
		for (char *p = line; p != result; p++)
			key[i++] = *p;
		key[i] = '\0';
		result++;

		result[strlen(result) - 1] = '\0';
		char *value = result;

		tierd_opcodes opcode = parse_token(key);
		if (opcode == BAD_OPTION)
			continue;

		if (!strlen(value)) {
			tierd_error("Cannot find file path\n");
			goto out;
		}

		if ((filename = basename(value)) == NULL) {
			tierd_error("Cannot parse filename\n");
			goto out;
		}

		if (access(value, X_OK) != 0) {
			tierd_error("Cannot execute file. Check filepath: %s\n",
					value);
			goto out;
		}

		switch (opcode) {
		case MLC_PATH:
			if (strcmp(filename, MLC_FILENAME)) {
				tierd_error("Wrong mlc path\n");
				goto out;
			}

			strncpy(mlc_file_path, value, strlen(value));
			mlc_file_path[strlen(value)] = '\0';

			tierd_info("MLC filepath: %s\n", mlc_file_path);
			break;
#ifdef ARCH_AMD
		case AMD_UPROFPCM_PATH:
			if (strcmp(filename, AMD_UPROFPCM_FILENAME)) {
				tierd_error("Wrong uprof path\n");
				goto out;
			}

			strncpy(amd_uprofpcm_file_path, value, strlen(value));
			amd_uprofpcm_file_path[strlen(value)] = '\0';

			tierd_info("AMDuProf PCM filepath: %s\n", amd_uprofpcm_file_path);
			break;
#endif
		default:
			break;
		}
	}

	if (mlc_file_path[0] == '\0') {
		tierd_error("Check MLC filepath configuration\n");
		goto out;
	}

#ifdef ARCH_AMD
	if (amd_uprofpcm_file_path[0] == '\0') {
		tierd_error("Check AMD uProf PCM filepath configuration\n");
		goto out;
	}
#endif

	ret = 0;
out:
	fclose(conf_fp);

	return ret;
}

static int register_daemon(void)
{
	int fd, ret;

	fd = open(TIERD_DEV_PATH, O_RDWR);
	if (fd < 0) {
		tierd_error("Failed to open device: %s\n", strerror(errno));
		return -1;
	}

	ret = ioctl(fd, TIERD_REGISTER_TASK);
	if (ret < 0) {
		tierd_error("Failed to ioctl: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

static inline int get_total_sockets()
{
	int socket_num;
	FILE *fp;
	char buffer[128];

	fp = popen("lscpu | grep 'Socket(s):'", "r");
	if (fp == NULL) {
		tierd_error("Fail to popen lscpu\n");
		return -1;
	}

	if (fgets(buffer, sizeof(buffer), fp) != NULL)
		socket_num = buffer[strlen(buffer) - 2] - '0';
	else {
		tierd_error("Fail to fget lscpu buffer\n");
		socket_num = -1;
	}

	pclose(fp);

	return socket_num;
}

int delete_state_file(const char *path)
{
	DIR *dir = opendir(path);
	size_t path_len = strlen(path);
	int result = -1;

	if (dir) {
		struct dirent *file;
		result = 0;

		while (!result && (file = readdir(dir))) {
			int status;
			char *file_path;
			size_t len;

			if (!strcmp(file->d_name, ".") || !strcmp(file->d_name, ".."))
				continue;

			len = path_len + strlen(file->d_name) + 2;
			file_path = malloc(sizeof(char) * len);

			if (file_path) {
				struct stat statbuf;
				snprintf(file_path, len, "%s/%s", path, file->d_name);

				if (!stat(file_path, &statbuf)) {
					if (S_ISDIR(statbuf.st_mode))
						status = delete_state_file(file_path);
					else
						status = remove(file_path);
					if (status)
						result = -1;
				}
				free(file_path);
			}
		}
		closedir(dir);
	} else {
		if (access(path, F_OK) != 0)
			return 0;
	}

	if (!result)
		result = rmdir(path);

	return result;
}

static int prepare_state_file()
{
	int nid;
	total_sockets = get_total_sockets();
	if (total_sockets == -1) {
		tierd_error("Fail to get total sockets\n");
		return -1;
	}

	total_nodes = numa_max_node() + 1;

	node_dir_path = calloc(total_nodes, sizeof(char *));
	if (node_dir_path == NULL) {
		tierd_error("Fail to allocate node_dir_path buffer\n");
		return -1;
	}
	for (nid = 0; nid < total_nodes; nid++) {
		node_dir_path[nid] = calloc(NODE_DIR_LEN, sizeof(char));
		if (node_dir_path[nid] == NULL) {
			tierd_error("Fail to allocate node_dir_path buffer\n");
			return -1;
		}
		sprintf(node_dir_path[nid], "%s/node%d", TIERD_DIR, nid);
	}

	return 0;
}

static void set_dummy_value(FILE *fd, const char *file_name)
{
	for (int i = 0; i < state_f_num; i++) {
		if (!strcmp(file_name, state_f[i])) {
			fprintf(fd, "true\n");
			return;
		}
	}

	for (int i = 0; i < bandwidth_f_num; i++) {
		if (!strcmp(file_name, bandwidth_f[i])) {
			fprintf(fd, "-1\n");
			return;
		}
	}

	for (int i = 0; i < fallback_order_f_num; i++) {
		if (!strcmp(file_name, fallback_order_f[i])) {
			fprintf(fd, "-1\n");
			return;
		}
	}
}

static int __create_state_file(const char *node_path, const char *file_name)
{
	FILE *fd;
	char full_path[64];

	sprintf(full_path, "%s/%s", node_path, file_name);

	fd = fopen(full_path, "w");
	if (fd == NULL) {
		tierd_error("Fail to create file %s: %s\n", full_path, strerror(errno));
		return -1;
	}

	set_dummy_value(fd, file_name);

	fclose(fd);

	return 0;
}

static int create_state_file()
{
	int i, j;

	if (access(TIERD_DIR, F_OK) != 0) {
		if (mkdir(TIERD_DIR, 0755) == -1) {
			tierd_error("Failed to create %s: %s\n", TIERD_DIR, strerror(errno));
			return -1;
		}
	}

	state_f_num = (int)(sizeof(state_f) / sizeof(state_f[0]));
	bandwidth_f_num = (int)(sizeof(bandwidth_f) / sizeof(bandwidth_f[0]));
	fallback_order_f_num = (int)(sizeof(fallback_order_f) /
			sizeof(fallback_order_f[0]));
	for (i = 0; i < total_nodes; i++) {
		if (access(node_dir_path[i], F_OK) != 0) {
			if (mkdir(node_dir_path[i], 0755) == -1) {
				tierd_error("Fail to create %s: %s\n",
						node_dir_path[i], strerror(errno));
				return -1;
			}
		}

		// Create Saturation State related Files.
		for (j = 0; j < state_f_num; j++)
			if (__create_state_file(node_dir_path[i], state_f[j]))
				return -1;

		// Create Max Bandwidth related Files.
		for (j = 0; j < bandwidth_f_num; j++)
			if (__create_state_file(node_dir_path[i], bandwidth_f[j]))
				return -1;

		// Create Fallback Order related Files for CPU Nodes.
		if (i < total_sockets) {
			for (j = 0; j < fallback_order_f_num; j++)
				if (__create_state_file(node_dir_path[i], fallback_order_f[j]))
					return -1;
		}
	}

	return 0;
}

static int initialize_state_files(void)
{
	if (prepare_state_file())
		goto prepare_fail;

	if (delete_state_file(TIERD_DIR))
		goto prepare_fail;

	if (create_state_file())
		goto create_fail;

	return 0;

create_fail:
	delete_state_file(TIERD_DIR);

prepare_fail:
	if (node_dir_path) {
		for (int nid = 0; nid < total_nodes; nid++) {
			if (node_dir_path[nid])
				free(node_dir_path[nid]);
		}
		free(node_dir_path);
	}

	return -1;
}

static void finalize_output_files(void)
{
	if (node_dir_path) {
		for (int nid = 0; nid < total_nodes; nid++) {
			if (node_dir_path[nid])
				free(node_dir_path[nid]);
		}
		free(node_dir_path);
	}

	delete_state_file(TIERD_DIR);
}

static inline int clean_up()
{
	int ret;

	tierd_info("Cleanup...\n");

	cleanup_start = true;

	if (monitor_thread) {
		tierd_info("Cleanup Monitor Thread Start\n");
		ret = pthread_join(monitor_thread, NULL);
		if (ret != 0)
			tierd_error("Failed to join monitor thread: %s\n", strerror(ret));
		tierd_info("Cleanup Monitor Thread Success\n");
	}

	if (threshold_thread) {
		tierd_info("Cleanup Threshold Thread Start\n");
		ret = kill_threshold_child();
		if (ret != 0)
			tierd_error("Failed to kill threshold child thread: %s\n",
					strerror(ret));
		pthread_mutex_lock(&mutex);
		pthread_cond_broadcast(&cond);
		while (!threshold_exit)
			pthread_cond_wait(&cond, &mutex);
		if (threshold_created == true && threshold_exit == false) {
			ret = pthread_cancel(threshold_thread);
			if (ret != 0)
				tierd_error("Failed to cancel threshold thread: %s\n",
						strerror(ret));
		}
		ret = pthread_join(threshold_thread, NULL);
		if (ret != 0)
			tierd_error("Failed to join threshold thread: %s\n", strerror(ret));
		pthread_mutex_unlock(&mutex);
		tierd_info("Cleanup Threshold Thread Success\n");
	}

	if (policy_thread) {
		tierd_info("Cleanup Policy Thread Start\n");
		ret = pthread_join(policy_thread, NULL);
		if (ret != 0)
			tierd_error("Failed to join policy thread (%d)\n", ret);
		tierd_info("Cleanup Policy Thread Success\n");
	}

	pthread_mutex_destroy(&mutex);
	pthread_cond_broadcast(&cond);
	pthread_cond_destroy(&cond);

	pthread_mutex_lock(&cleanup_mutex);
	cleanup_fin = true;
	pthread_cond_signal(&cleanup_cond);
	pthread_mutex_unlock(&cleanup_mutex);

	tierd_info("Cleanup State File Start\n");
	finalize_output_files();
	tierd_info("Cleanup State File Success\n");

	return EXIT_SUCCESS;
}

static void signal_handler(int signum)
{
	int ret;
	switch (signum) {
	case SIGUSR1:
		tierd_info("Restart Threshold Measurement...\n");
		stop_event = true;
		kill_threshold_child();

		pthread_mutex_lock(&mutex);

		total_nodes = numa_max_node() + 1;

		threshold_fin = false;
		pthread_cond_signal(&cond);

		stop_event = false;

		pthread_mutex_unlock(&mutex);
		break;
	case SIGINT:
		if (cleanup_start)
			break;
		tierd_info("Terminate...\n");
		ret = clean_up();
		exit(ret);
		break;
	default:
		tierd_info("Signal %d Received...\n", signum);
		break;
	}
}

static inline void init_signal_handler(struct sigaction *sact)
{
	sigemptyset(&sact->sa_mask);
	sact->sa_handler = signal_handler;
	sact->sa_flags = 0;
	sigaction(SIGUSR1, sact, NULL);
	sigaction(SIGUSR2, sact, NULL);
	sigaction(SIGINT, sact, NULL);
}

int main(int argc, char *argv[])
{
	int opt, ret, signum;
	int input_window = -1, input_interval = -1;
	struct sigaction sact;

	init_signal_handler(&sact);

	max_window = MAX_WINDOW;
	interval_in_us = INTERVAL;

	while ((opt = getopt(argc, argv, "i:w:c:hD")) != -1) {
		switch (opt) {
		case 'D':
			daemonize = 1;
			break;
		case 'c':
			config_file_name = optarg;
			break;
		case 'w':
			input_window = atoi(optarg);
			if (input_window < 1) {
				tierd_error("Positive number is allowed in max_window size\n");
				print_usage();
				break;
			}
			max_window = input_window;
			break;
		case 'i':
			input_interval = atoi(optarg);
			/*
			 * If interval is set to under 1,000,000us,
			 * Live CXL BW value by PMU is reported strangely too frequently.
			 */
			if (input_interval < 1000000) {
				tierd_error("Interval should be larger than 1,000,000us\n");
				print_usage();
				break;
			}
			interval_in_us = input_interval;
			break;
		case 'h':
		default:
			print_usage();
			break;
		}
	}

	tierd_info("Input Parameter Info\n");
	tierd_info("max_window: %d\n", max_window);
	tierd_info("interval_in_us: %d\n", interval_in_us);

	log_init(daemonize ? 0 : 1);

	if (parse_daemon_config(config_file_name)) {
		tierd_error("Failed to load config\n");
		return EXIT_FAILURE;
	}

	if (daemonize && daemon(0, 0)) {
		tierd_error("Failed to daemonize: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = register_daemon();
	if (ret < 0) {
		tierd_error("Failed to register daemon to driver\n");
		exit(EXIT_FAILURE);
	}

	ret = initialize_state_files();
	if (ret < 0) {
		tierd_error("Failed to initialize output files\n");
		exit(EXIT_FAILURE);
	}

	if (!cleanup_start) {
		ret = pthread_create(&monitor_thread, NULL, monitor_func, NULL);
		if (ret != 0) {
			tierd_error("Failed to create monitor thread (%s)\n", strerror(ret));
			return EXIT_FAILURE;
		}
	}

	if (!cleanup_start) {
		pthread_mutex_lock(&sync_mutex);
		ret = pthread_create(&threshold_thread, NULL, threshold_func, NULL);
		if (ret != 0) {
			tierd_error("Failed to create threshold thread (%s)\n",
					strerror(ret));
			return EXIT_FAILURE;
		}
		while (!threshold_created)
			pthread_cond_wait(&sync_cond, &sync_mutex);
		pthread_mutex_unlock(&sync_mutex);
	}

	if (!cleanup_start) {
		ret = pthread_create(&policy_thread, NULL, policy_func, NULL);
		if (ret != 0) {
			tierd_error("Failed to create policy thread (%s)\n", strerror(ret));
			return EXIT_FAILURE;
		}
	}

	while (!cleanup_start) {
		ret = sigwait(&sact.sa_mask, &signum);
		if (ret != 0) {
			tierd_error("Failed to wait signal: %s\n", strerror(errno));
			goto exit;
		}
		switch (signum) {
		case SIGINT:
			tierd_info("Terminate....\n");
			goto exit;
		case SIGUSR1:
			// Get Memory Node On/Offline Event from Driver
			tierd_info("Start Threshold Measurement....\n");
			kill_threshold_child();
			pthread_mutex_lock(&mutex);

			total_nodes = numa_max_node() + 1;
			threshold_fin = false;
			pthread_cond_signal(&cond);

			while (!threshold_fin)
				pthread_cond_wait(&cond, &mutex);
			pthread_mutex_unlock(&mutex);
			break;
		default:
			tierd_info("Signal %d Received...\n", signum);
			break;
		}
	}

exit:
	if (!cleanup_start)
		ret = clean_up();
	else {
		if (!cleanup_fin) {
			pthread_mutex_lock(&cleanup_mutex);
			while (!cleanup_fin)
				pthread_cond_wait(&cleanup_cond, &cleanup_mutex);
			pthread_mutex_unlock(&cleanup_mutex);
		}
	}

	pthread_mutex_destroy(&cleanup_mutex);
	pthread_cond_broadcast(&cleanup_cond);
	pthread_cond_destroy(&cleanup_cond);

	return ret;
}
