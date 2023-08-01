#include <stdio.h>
#include <stdbool.h>
#include <numa.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "common.h"
#include "util.h"
#include "monitor.h"

#define UPROF_BUF_SIZE 4096
// Must be set by pow of two for bitwise operation.
#define UPROF_HISTORY_SIZE 1024

#define IS_DIGIT(x) ('0' <= (x) && (x) <= '9')

int head;

static int epoch;

enum {
	READ = 0,
	WRITE
};

struct monitor_operations {
	long (*init)(void);
	void (*start)(void);
	void (*stop)(void);
	int (*get_BW)(int node_id, float *read_bw, float *write_bw);
	bool (*restart)(float read_bw, float write_bw);
};

static struct monitor_operations monitor_ops = {
	.init = NULL,
	.start = NULL,
	.stop = NULL,
	.get_BW = NULL,
	.restart = NULL,
};

#ifdef ARCH_INTEL
static bool pcm_monitor_restart(float read_bw, float write_bw)
{
	static int count = 0;
	const int restart_count = 10;

	if (read_bw != 0.0f || write_bw != 0.0f) {
		count = 0;
		return false;
	}

	if (count++ > restart_count * total_nodes) {
		count = 0;
		return true;
	}

	return false;
}
#endif

#ifdef ARCH_AMD
static pthread_t uprof_monitor_thread;
static FILE *uprof_fp;

static int uprof_chl_num = 0;
static int uprof_skt_num = -1;
static int uprof_history_head = 0;

static int *uprof_chl_idx;

static float ***uprof_history;

enum UPROF_META_STATUS{
	FINISH = 0, // Already Done. Keep going next step.
	CONTINUE, // We don't need this line. Move to the next line.
	ERROR, // Something Error (e.g. malloc fail)
};

static int uprof_set_num_of_socket(char *line)
{
	if (!line)
		return ERROR;

	if (uprof_skt_num != -1)
		return FINISH;

	int cnt = 0, idx = 0;
	int ret = CONTINUE;
	int len = (int)strlen(line);

	for (int i = 0; i < len; i++) {
		if (line[i] == ',') {
			idx = i + 1;
			cnt++;
		}
		if (cnt == 2)
			goto exit;
	}

	char *socket_line = strstr(line, "Number of Sockets :,");
	if (socket_line == NULL)
		goto exit;

	char socket[16];
	cnt = 0;
	for (int i = idx; i < len; i++) {
		if (IS_DIGIT(line[i]))
			socket[cnt++] = line[i];
	}
	socket[cnt] = '\0';

	uprof_skt_num = atoi(socket);
	ret = FINISH;

exit:
	return ret;
}

static int uprof_set_history()
{
	if (uprof_history != NULL)
		return FINISH;

	uprof_history = (float ***)calloc(UPROF_HISTORY_SIZE, sizeof(float **));
	if (uprof_history == NULL) {
		bwd_error("%s: calloc fail\n", __func__);
		return ERROR;
	}
	for (int i = 0; i < UPROF_HISTORY_SIZE; i++) {
		uprof_history[i] = (float **)calloc(uprof_skt_num, sizeof(float *));
		if (uprof_history[i] == NULL) {
			bwd_error("%s: calloc fail\n", __func__);
			return ERROR;
		}
		for (int j = 0; j < uprof_skt_num; j++) {
			uprof_history[i][j] = (float *)calloc(2, sizeof(float *));
			if (uprof_history[i][j] == NULL) {
				bwd_error("%s: calloc fail\n", __func__);
				return ERROR;
			}
		}
	}

	return FINISH;
}

static void uprof_destroy_history()
{
	if (uprof_history) {
		for (int i = 0; i < UPROF_HISTORY_SIZE; i++) {
			for (int j = 0; j < uprof_skt_num; j++) {
				if (uprof_history[i][j])
					free(uprof_history[i][j]);
			}
			if (uprof_history[i])
				free(uprof_history[i]);
		}
		free(uprof_history);
		uprof_history = NULL;
	}
}

static int uprof_set_num_of_channel(char *line)
{
	if (!line)
		return ERROR;

	if (uprof_chl_idx != NULL)
		return FINISH;

	char *package_line = strstr(line, "Package-");
	if (package_line == NULL)
		return CONTINUE;

	uprof_chl_idx = (int *)malloc(sizeof(int) * uprof_skt_num);
	if (uprof_chl_idx == NULL)
		return ERROR;

	int len = (int)strlen(line);
	for (int i = 0; i < len; i++) {
		if (line[i] != ',')
			continue;
		uprof_chl_num++;
	}

	/*
	 * We only want to get socket's [ total bw, read bw, write bw ].
	 * But uProf reports whole channels's b/w like below.
	 * Package-total bw,read bw,write bw,chl A's read bw,chl A's write bw, ...,
	 * So, we save each socket's total bw index.
	 */
	int adjust = 0, skt = 0;
	for (int i = 1; i < len; i++) {
		if (line[i] == line[i - 1] && line[i] == ',')
			continue;

		if (line[i] != ',')
			adjust++;

		// Start Index of each socket's total bw
		if (line[i] == ',' && line[i - 1] != ',') {
			int start = i - adjust - 1;
			uprof_chl_idx[skt] = start;
			skt++;
		}
	}

	// Don't need to keep going to the next process for this line. Just skip.
	return CONTINUE;
}

static int uprof_check_meta(char *line)
{
	int ret;

	if (uprof_skt_num != -1 && uprof_history != NULL
			&& uprof_chl_idx != NULL)
		return FINISH;

	// Return FINISH or CONTINUE
	ret = uprof_set_num_of_socket(line);
	if (ret == CONTINUE)
		return ret;

	// Return FINISH or ERROR
	ret = uprof_set_history();
	if (ret == ERROR)
		return ret;

	// Return FINISH or CONTINUE or ERROR
	ret = uprof_set_num_of_channel(line);

	return ret;
}

static int uprof_check_validation(char *line)
{
	/*
	 * We need to check the validation of line. For example,
	 * For example, valid line's number of semicolon is same with header's one.
	 * But, some invalid line's number of semicolon isn't like below.
	 * 	"0.00, ...,\n" // Not begin with total bw but has '\n'.
	 */
	int chl_num = 0;
	int len = (int)strlen(line);
	for (int i = 0; i < len; i++) {
		if (line[i] == ',')
			chl_num++;
	}

	// Invalid line
	if (chl_num != uprof_chl_num)
		return CONTINUE;

	return FINISH;
}

static int uprof_split_line(char *line, char ***line_split)
{
	/*
	 * Input : "12.00,6.00,6.00,0.00, ...,";
	 * Output :
	 *			[0] : "12.00"
	 *			[1] : "6.00"
	 *			[2] : "6.00"
	 *			[3] : "0.00"
	 *			... // The number of channels
	 */

	// Don't need to parse unneeded line.
	if (!IS_DIGIT(line[0]))
		return CONTINUE;

	(*line_split) = (char **)calloc(uprof_chl_num, sizeof(char*));
	if ((*line_split) == NULL) {
		bwd_error("%s: calloc fail\n", __func__);
		return ERROR;
	}
	for (int i = 0; i < uprof_chl_num; i++) {
		(*line_split)[i] = (char *)calloc(16, sizeof(char));
		if ((*line_split)[i] == NULL) {
			bwd_error("%s: calloc fail\n", __func__);
			return ERROR;
		}
	}

	int row = 0, col = 0;
	int pos = (int)strlen(line);
	for (int i = 0; i < pos; i++) {
		if (line[i] == '\n')
			break;

		if (line[i] == ',') {
			(*line_split)[row++][col] = '\0';
			col = 0;
			continue;
		}
		(*line_split)[row][col++] = line[i];
	}

	return FINISH;
}

static void uprof_get_bw(char **line_split)
{
	float read_bw, write_bw;
	for (int i = 0; i < uprof_skt_num; i++) {
		int idx = uprof_history_head & (UPROF_HISTORY_SIZE - 1);
		read_bw = strtod(line_split[uprof_chl_idx[i] + 1], NULL);
		write_bw = strtod(line_split[uprof_chl_idx[i] + 2], NULL);
		uprof_history[idx][i][READ] = read_bw;
		uprof_history[idx][i][WRITE] = write_bw;
	}
	uprof_history_head++;

	for (int i = 0; i < uprof_chl_num; i++) {
		if (line_split[i])
			free(line_split[i]);
	}
	if (line_split)
		free(line_split);
}

static void *uprof_monitor_func(void *args __attribute((unused)))
{
	char buffer[UPROF_BUF_SIZE] = { '\0', };
	size_t read_bytes = 0;

	while((read_bytes = read(fileno(uprof_fp), buffer, sizeof(buffer))) != 0) {
		if (unlikely(cleanup_start))
			break;

		int max_row = 0;
		for (int i = 0; i < (int)read_bytes; i++) {
			if (buffer[i] == '\n')
				max_row++;
		}

		char **lines = (char **)calloc(max_row, sizeof(char *));
		if (lines == NULL) {
			bwd_error("%s: calloc fail\n", __func__);
			goto exit;
		}
		for (int i = 0; i < max_row; i++) {
			lines[i] = (char *)calloc(UPROF_BUF_SIZE, sizeof(char));
			if (lines[i] == NULL) {
				bwd_error("%s: calloc fail\n", __func__);
				goto exit;
			}
		}
		int row = 0, col = 0;
		for (int i = 0; i < (int)read_bytes; i++) {
			/*
			 * '\n' is only sentinel to separate uProf results into line by line.
			 * But we need to check the format of each lines.
			 * For example, format of good line is below.
			 * 	"12.00, 6.00, 6.00, 0.00, ..., \n"  // '\n' is exists.
			 * But some wrong line also be like below.
			 *  "12.00, 6.00, ..., 0.0" // '\n' is not exists.
			 * In this case, we discard wrong lines.
			 */
			if (row == max_row)
				break;
			if (buffer[i] == '\n') {
				lines[row++][col] = '\0';
				col = 0;
				continue;
			}
			lines[row][col++] = buffer[i];
		}

		for (int i = 0; i < max_row; i++) {
			char *line = lines[i];
			int ret;

			// Return FINISH or CONTINUE or ERROR
			ret = uprof_check_meta(line);
			if (ret == ERROR)
				goto exit;

			if (ret == CONTINUE)
				continue;

			// Return FINISH or CONTINUE
			ret = uprof_check_validation(line);
			if (ret == CONTINUE)
				continue;

			// Return FINISH or ERROR
			char **line_split = NULL;
			ret = uprof_split_line(line, &line_split);
			if (ret == ERROR)
				goto exit;
			
			if (ret == CONTINUE)
				continue;

			// Now we can get bandwidth.
			uprof_get_bw(line_split);
		}
		memset(buffer, '\0', UPROF_BUF_SIZE);
		for (int i = 0; i < max_row; i++) {
			if (lines[i])
				free(lines[i]);
		}
		if (lines) {
			free(lines);
			lines = NULL;
		}
	}
exit:
	pthread_exit(NULL);
}

static long uprof_monitor_init(void)
{
	int ret = -1;
	char uprof_cmd[MAX_PATH_LEN + 256] = { '\0', };

	snprintf(uprof_cmd, sizeof(uprof_cmd),
				"%s -m memory -d -1 -a", amd_uprofpcm_file_path);

	uprof_fp = popen(uprof_cmd, "r");
	if (uprof_fp == NULL) {
		bwd_error("uprof popen failed\n");
		return ret;
	}

	ret = pthread_create(&uprof_monitor_thread, NULL, uprof_monitor_func, NULL);
	if (ret != 0)
		bwd_error("Failed to create uprof monitor thread: %s\n", strerror(ret));

	return ret;
}

static void uprof_monitor_start(void)
{
	// Unlike PCM, uProf requires nothing to prepare when start.
	return;
}

static void uprof_monitor_stop(void)
{
	// TODO - Check the join return
	int ret = pthread_join(uprof_monitor_thread, NULL);
	if (ret != 0)
		bwd_error("Failed to join uprof monitor thread: %s\n", strerror(ret));

	if (uprof_fp)
		pclose(uprof_fp);

	if (uprof_chl_idx) {
		free(uprof_chl_idx);
		uprof_chl_idx = NULL;
	}

	uprof_destroy_history();
}

static int uprof_monitor_get_BW(int nid, float *read_bw, float *write_bw)
{
	if (nid >= uprof_skt_num || nid < 0)
		return ERROR;

	int idx = uprof_history_head - 1 + UPROF_HISTORY_SIZE;
	idx &= (UPROF_HISTORY_SIZE - 1);
	// The formula in the upper line will prevent idx from becoming negative.
	if (idx < 0)
		return ERROR;

	*read_bw = uprof_history[idx][nid][READ];
	*write_bw = uprof_history[idx][nid][WRITE];

	return 0;
}

static bool uprof_monitor_restart(float read_bw __attribute__((unused)),
		float write_bw __attribute__((unused)))
{
	/* TODO
	 * Check the case exists, uProf process alive but report only zero like pcm.
	 * Now, we don't confirm that case exists in uProf, so just return false...
	 */
	return false;
}
#endif

static void set_monitor_func()
{
#if defined(ARCH_INTEL)
	monitor_ops.init = pcm_monitor_init;
	monitor_ops.start = pcm_monitor_start;
	monitor_ops.stop = pcm_monitor_stop;
	monitor_ops.get_BW = pcm_monitor_get_BW;
	monitor_ops.restart = pcm_monitor_restart;
#elif defined(ARCH_AMD)
	monitor_ops.init = uprof_monitor_init;
	monitor_ops.start = uprof_monitor_start;
	monitor_ops.stop = uprof_monitor_stop;
	monitor_ops.get_BW = uprof_monitor_get_BW;
	monitor_ops.restart = uprof_monitor_restart;
#else
#error "Please check cpu vendor id"
#endif
}

void *monitor_func(void *args __attribute__((unused)))
{
	bwd_info("Run Monitor Thread\n");

	int nid;
	float read_bw, write_bw;

	set_monitor_func();

restart:
	if (monitor_ops.init()) {
		bwd_error("Failed to initialize monitor thread\n");
		pthread_exit(NULL);
	}

	monitor_ops.start();
	while (1) {
		if (unlikely(cleanup_start))
			break;

		head = epoch % max_window;
		for (nid = 0; nid < total_nodes; nid++) {
			read_bw = 0.0f; write_bw = 0.0f;
			if (monitor_ops.get_BW(nid, &read_bw, &write_bw))
				continue;

			if (monitor_ops.restart(read_bw, write_bw)) {
				bwd_info("Restart Monitor Thread\n");
				monitor_ops.stop();
				goto restart;
			}

			/*
			 * Modify pcm return underflow-like BW results
			 * when run pcm repeatly (multiple pcm process).
			 * Because PCI PMU Reset triggered by another pcm process.
			 * Prevent this issue by with PCM_USE_PERF=1 before run pcm.
			 * But PCM_USE_PERF=0, we should refine it by ourselves.
			 */
			if ((read_bw > spike[nid]) || isnan(read_bw))
				read_bw = 0.0f;
			if ((write_bw > spike[nid]) || isnan(write_bw))
				write_bw = 0.0f;
			cur_bw[head][nid][BW_READ] = read_bw;
			cur_bw[head][nid][BW_WRITE] = write_bw;
			cur_bw[head][nid][BW_RDWR] = read_bw + write_bw;
		}
		epoch++;

		usleep(interval);
	}
	monitor_ops.stop();

	pthread_exit(NULL);
}
