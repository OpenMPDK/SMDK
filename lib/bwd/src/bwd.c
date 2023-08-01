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
int total_nodes;
int max_window;
int threshold;
int interval;
int stop_event;

char mlc_file_path[MAX_PATH_LEN];
#ifdef ARCH_AMD
char amd_uprofpcm_file_path[MAX_PATH_LEN];
#endif
char *config_file_name = CONFIG_FILE_PATH;
char **output_file_names;

static void print_usage(void)
{
	fprintf(stderr,
			"Usage: bwd [-hD] [-c config_file]\n");
	exit(EXIT_FAILURE);
}

typedef enum {
	BAD_OPTION,
	MLC_PATH,
#ifdef ARCH_AMD
	AMD_UPROFPCM_PATH
#endif
} bwd_opcodes;

static struct {
	const char *name;
	bwd_opcodes opcode;
} config_keywords[] = {
	{ "MLC_PATH", MLC_PATH },
#ifdef ARCH_AMD
	{ "AMD_UPROFPCM_PATH", AMD_UPROFPCM_PATH },
#endif
	{ NULL, BAD_OPTION }
};

static bwd_opcodes parse_token(const char *cp)
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
		bwd_error("Failed to open config file\n");
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

		bwd_opcodes opcode = parse_token(key);
		if (opcode == BAD_OPTION)
			continue;

		if (!strlen(value)) {
			bwd_error("Cannot find file path\n");
			goto out;
		}

		if ((filename = basename(value)) == NULL) {
			bwd_error("Cannot parse filename\n");
			goto out;
		}

		if (access(value, X_OK) != 0) {
			bwd_error("Cannot execute file. Check filepath: %s\n",
					value);
			goto out;
		}

		switch (opcode) {
		case MLC_PATH:
			if (strcmp(filename, MLC_FILENAME)) {
				bwd_error("Wrong mlc path\n");
				goto out;
			}

			strncpy(mlc_file_path, value, strlen(value));
			mlc_file_path[strlen(value)] = '\0';

			bwd_info("MLC filepath: %s\n", mlc_file_path);
			break;
#ifdef ARCH_AMD
		case AMD_UPROFPCM_PATH:
			if (strcmp(filename, AMD_UPROFPCM_FILENAME)) {
				bwd_error("Wrong uprof path\n");
				goto out;
			}

			strncpy(amd_uprofpcm_file_path, value, strlen(value));
			amd_uprofpcm_file_path[strlen(value)] = '\0';

			bwd_info("AMDuProf PCM filepath: %s\n", amd_uprofpcm_file_path);
			break;
#endif
		default:
			break;
		}
	}

	if (mlc_file_path[0] == '\0') {
		bwd_error("Check MLC filepath configuration\n");
		goto out;
	}

#ifdef ARCH_AMD
	if (amd_uprofpcm_file_path[0] == '\0') {
		bwd_error("Check AMD uProf PCM filepath configuration\n");
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

	fd = open(BWD_DEV_PATH, O_RDWR);
	if (fd < 0) {
		bwd_error("Failed to open device: %s\n", strerror(errno));
		return -1;
	}

	ret = ioctl(fd, BWD_REGISTER_TASK);
	if (ret < 0) {
		bwd_error("Failed to ioctl: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

static void truncate_run_directory(void)
{
	int nid;
	char filepath[MAX_PATH_LEN];
	struct stat st;

	for (nid = 0; nid < NUMA_NUM_NODES; nid++) {
		snprintf(filepath, sizeof(filepath), "%s/node%d", BWD_DIR, nid);

		if (stat(filepath, &st) == 0)
			unlink(filepath);
	}

	if (stat(BWD_DIR, &st) == 0)
		remove(BWD_DIR);
}

static int initialize_output_files(void)
{
	struct stat st;
	int i;
	FILE *fd;

	total_nodes = numa_max_node() + 1;
	output_file_names = calloc(total_nodes, sizeof(char *));
	if (output_file_names == NULL) {
		bwd_error("Fail to allocate output_file_names buffer\n");
		return -1;
	}

	for (i = 0; i < total_nodes; i++) {
		output_file_names[i] = calloc(MAX_PATH_LEN, sizeof(char));
		if (output_file_names[i] == NULL) {
			bwd_error("Fail to allocate output_file_name buffer\n");
			goto fail;
		}
		sprintf(output_file_names[i], "%s/node%d", BWD_DIR, i);
	}

	truncate_run_directory();

	if (stat(BWD_DIR, &st) != 0) {
		if (mkdir(BWD_DIR, 0755) == -1) {
			bwd_error("Failed to create directory: %s: %s\n", BWD_DIR,
					strerror(errno));
			goto fail;
		}
	}

	for (i = 0; i < total_nodes; i++) {
		fd = fopen(output_file_names[i], "w");
		if (fd == NULL) {
			bwd_error("Failed to open output file %s: %s\n", output_file_names[i],
					strerror(errno));
			goto fail;
		}

		fprintf(fd, "false");
		fclose(fd);
	}

	return 0;

fail:
	if (output_file_names) {
		for (i = 0; i < total_nodes; i++) {
			if (output_file_names[i])
				free(output_file_names[i]);
		}
		free(output_file_names);
	}

	return -1;
}

static void finalize_output_files(void)
{
	int nid;

	if (output_file_names) {
		for (nid = 0; nid < total_nodes; nid++)
			if (output_file_names[nid])
				free(output_file_names[nid]);

		free(output_file_names);
	}

	truncate_run_directory();
}

static inline int clean_up()
{
	int ret;

	bwd_info("Cleanup...\n");

	cleanup_start = true;

	finalize_output_files();

	if (monitor_thread) {
		bwd_info("Cleanup Monitor Thread Start\n");
		ret = pthread_join(monitor_thread, NULL);
		if (ret != 0)
			bwd_error("Failed to join monitor thread: %s\n", strerror(ret));
		bwd_info("Cleanup Monitor Thread Success\n");
	}

	if (threshold_thread) {
		bwd_info("Cleanup Threshold Thread Start\n");
		ret = kill_threshold_child();
		if (ret != 0)
			bwd_error("Failed to kill threshold child thread: %s\n", strerror(ret));
		pthread_mutex_lock(&mutex);
		pthread_cond_broadcast(&cond);
		while (!threshold_exit)
			pthread_cond_wait(&cond, &mutex);
		if (threshold_created == true && threshold_exit == false) {
			ret = pthread_cancel(threshold_thread);
			if (ret != 0)
				bwd_error("Failed to cancel threshold thread: %s\n", strerror(ret));
		}
		ret = pthread_join(threshold_thread, NULL);
		if (ret != 0)
			bwd_error("Failed to join threshold thread: %s\n", strerror(ret));
		pthread_mutex_unlock(&mutex);
		bwd_info("Cleanup Threshold Thread Success\n");
	}

	if (policy_thread) {
		bwd_info("Cleanup Policy Thread Start\n");
		ret = pthread_join(policy_thread, NULL);
		if (ret != 0)
			bwd_error("Failed to join policy thread (%d)\n", ret);
		bwd_info("Cleanup Policy Thread Success\n");
	}

	pthread_mutex_destroy(&mutex);
	pthread_cond_broadcast(&cond);
	pthread_cond_destroy(&cond);

	pthread_mutex_lock(&cleanup_mutex);
	cleanup_fin = true;
	pthread_cond_signal(&cleanup_cond);
	pthread_mutex_unlock(&cleanup_mutex);

	return EXIT_SUCCESS;
}

static void signal_handler(int signum)
{
	int ret;
	switch(signum) {
	case SIGUSR1:
		bwd_info("Restart Threshold Measurement...\n");
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
		bwd_info("Terminate...\n");
		ret = clean_up();
		exit(ret);
		break;
	default:
		bwd_info("Signal %d Received...\n", signum);
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
	struct sigaction sact;

	// TODO - If user parameter exists, uses it. If not, using macro.
	max_window = MAX_WINDOW;
	interval = INTERVAL;

	init_signal_handler(&sact);

	while ((opt = getopt(argc, argv, "c:hD")) != -1) {
		switch (opt) {
			case 'D':
				daemonize = 1;
				break;
			case 'c':
				config_file_name = optarg;
				break;
			case 'h':
			default:
				print_usage();
				break;
		}
	}

	log_init(daemonize ? 0 : 1);

	if (parse_daemon_config(config_file_name)) {
		bwd_error("Failed to load config\n");
		return EXIT_FAILURE;
	}

	if (daemonize && daemon(0, 0)) {
		bwd_error("Failed to daemonize: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = register_daemon();
	if (ret < 0) {
		bwd_error("Failed to register daemon to driver\n");
		exit(EXIT_FAILURE);
	}

	ret = initialize_output_files();
	if (ret < 0) {
		bwd_error("Failed to initialize output files\n");
		exit(EXIT_FAILURE);
	}

	if (!cleanup_start) {
		ret = pthread_create(&monitor_thread, NULL, monitor_func, NULL);
		if (ret != 0) {
			bwd_error("Failed to create monitor thread (%s)\n", strerror(ret));
			return EXIT_FAILURE;
		}
	}

	if (!cleanup_start) {
		pthread_mutex_lock(&sync_mutex);
		ret = pthread_create(&threshold_thread, NULL, threshold_func, NULL);
		if (ret != 0) {
			bwd_error("Failed to create threshold thread (%s)\n", strerror(ret));
			return EXIT_FAILURE;
		}
		while (!threshold_created)
			pthread_cond_wait(&sync_cond, &sync_mutex);
		pthread_mutex_unlock(&sync_mutex);
	}

	if (!cleanup_start) {
		ret = pthread_create(&policy_thread, NULL, policy_func, NULL);
		if (ret != 0) {
			bwd_error("Failed to create policy thread (%s)\n", strerror(ret));
			return EXIT_FAILURE;
		}
	}

	while (!cleanup_start) {
		ret = sigwait(&sact.sa_mask, &signum);
		if (ret != 0) {
			bwd_error("Failed to wait signal: %s\n", strerror(errno));
			goto exit;
		}
		switch(signum) {
		case SIGINT:
			bwd_info("Terminate....\n");
			goto exit;
		case SIGUSR1:
			// Get Memory Node On/Offline Event from Driver
			bwd_info("Start Threshold Measurement....\n");
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
			bwd_info("Signal %d Received...\n", signum);
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
