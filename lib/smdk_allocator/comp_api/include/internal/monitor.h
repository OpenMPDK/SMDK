#ifndef MONITOR_H_
#define MONITOR_H_

#define EPS (1e-10)

#define SET_MEMPOLICY2(a, b) syscall(454, a, b)

struct mempolicy_args {
	unsigned short mode;
	unsigned long *nodemask;
	unsigned long maxnode;
	unsigned short flags;
	struct {
		/* Memory allowed */
		struct {
			unsigned long maxnode;
			unsigned long *nodemask;
		} allowed;
		/* Address information */
		struct {
			unsigned long addr;
			unsigned long node;
			unsigned short mode;
			unsigned short flags;
		} addr;
		/* Interleave */
	} get;
	/* Mode specific settings */
	union {
		struct {
			unsigned long next_node; /* get only */
		} interleave;
		/* Partial interleave */
		struct {
			unsigned long weight;  /* get and set */
			unsigned long next_node; /* get only */
		} pil;
		/* Weighted interleave */
		struct {
			unsigned long next_node; /* get only */
			unsigned char *weights; /* get and set */
		} wil;
	};
};

int init_monitor_thread(void);
int get_best_pool_id(int socket_id);

static inline void free_arr_2d(void **arr, int row)
{
	if (arr) {
		for (int i = 0; i < row; i++) {
			if (arr[i]) {
				free(arr[i]);
				arr[i] = NULL;
			}
		}
		free(arr);
		arr = NULL;
	}
}

static inline void free_arr_3d(void ***arr, int depth, int row)
{
	if (arr) {
		for (int i = 0; i < depth; i++) {
			if (arr[i])
				free_arr_2d(arr[i], row);
		}
		free(arr);
		arr = NULL;
	}
}

static inline void *alloc_arr(int size, size_t typeSize)
{
	void *arr = calloc(size, typeSize);
	if (!arr)
		return NULL;

	return arr;
}

static inline void **alloc_arr_2d(int row, int col, size_t typeSize)
{
	void **arr = calloc(row, sizeof(void *));
	if (!arr)
		return NULL;

	for (int i = 0; i < row; i++) {
		arr[i] = alloc_arr(col, typeSize);
		if (!arr[i]) {
			free_arr_2d(arr, i - 1);
			return NULL;
		}
	}

	return arr;
}

static inline void ***alloc_arr_3d(int depth, int row, int col, size_t typeSize)
{
	void ***arr = calloc(depth, sizeof(void **));
	if (!arr)
		return NULL;

	for (int i = 0; i < depth; i++) {
		arr[i] = alloc_arr_2d(row, col, typeSize);
		if (!arr[i]) {
			free_arr_3d(arr, i - 1, row);
			return NULL;
		}
	}

	return arr;
}

static inline bool strtobool(const char *str)
{
	if (str == NULL || *str == '\0')
		return false;
	else if (strncasecmp(str, "true", 4) == 0)
		return true;
	else if (strncasecmp(str, "false", 5) == 0)
		return false;
	else
		return false;
}

static inline void parse_order_string(char *s, int *arr, int max_size)
{
	char *save_ptr;
	char *token = strtok_r(s, ",", &save_ptr);
	int i = 0;
	while (token != NULL && i < max_size) {
		arr[i++] = atoi(token);
		token = strtok_r(NULL, ",", &save_ptr);
	}
}

static inline int get_total_sockets()
{
	FILE *file;
	struct bitmask *bmp_has_cpu;
	char path[MAX_CHAR_LEN] = { 0, };
	char has_cpu[MAX_CHAR_LEN] = { 0, };

	sprintf(path, "/sys/devices/system/node/has_cpu");
	file = fopen(path, "r");
	if (file == NULL)
		return -1;
	fgets(has_cpu, MAX_CHAR_LEN, file);
	strtok(has_cpu, "\n");
	fclose(file);

	bmp_has_cpu = numa_parse_nodestring(has_cpu);
	for (int i = bmp_has_cpu->size; i >= 0; i--) {
		if (!numa_bitmask_isbitset(bmp_has_cpu, i))
			continue;
		return i + 1;
	}

	return -1;
}

#endif /* MONITOR_H_ */
