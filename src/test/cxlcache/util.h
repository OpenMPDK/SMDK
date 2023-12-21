#include <stdio.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <wait.h>
#include <numa.h>

#define ENV_SET_FAIL (2)
#define TEST_FAILURE (1)
#define TEST_SUCCESS (0)

#define INPUT_ERR(s) { input_help_message(s); return 2; }

#define KB (1 << 10L)
#define MB (1 << 20L)
#define GB (1 << 30L)
#define UNIT_STR(u) (u >= GB) ? 'G' : (u >= MB) ? 'M' : 'K'
#define UNIT_BIT(u) (u == 'G') ? 30 : (u == 'M') ? 20 : 10

#define TEST_NUM 5
#define MAX_THREADS 20

typedef uint64_t u64;
typedef struct __test_info {
	int (*test_func)();
	const char *name;
} test_info_t;

typedef struct __thread_data {
	char *path;
	int ret;
} thread_data_t;

int (*test_func)();
int put_get_correctness(); 
int modify_put_get_correctness();
int multi_thread();
int multi_process();
int put_cxl_page();

test_info_t test_list[TEST_NUM] = {
	{ &put_get_correctness, "put_get_correctness" },
	{ &modify_put_get_correctness, "modify_put_get_correctness" },
	{ &multi_thread, "multi_thread" },
	{ &multi_process, "multi_process" },
	{ &put_cxl_page, "put_cxl_page" }
};

pid_t pid;

int num_threads;
int noop_node;
int test_idx = -1;

u64 size;
u64 limit = INT_MAX;

int cxlcache_enabled;

char test_path[PATH_MAX];

void input_help_message()
{
	printf("\n");
	printf("Max test file size : %lu KB, %lu MB, %lu GB", limit / 1024, 
		limit / 1024 / 1024, limit / 1024 / 1024 / 1024);
	printf("\n\n");
	printf("Can set test file size at run_cxlcache_XXX_test.sh\n");
	printf("e.g. for i in 256m 512m 1g -> Testing by 256MB, 512MB, 1GB files");
	printf("\n\n");
	printf("Can set # of threads at run_cxlcache_multithread_test.sh\n");
	printf("e.g. $CXLCACHE_TEST $i multi_thread $TCDIR 10 -> Testing by 10 threads");
	printf("\n\n");
	printf("Support test list below\n");
	printf("put_get_correctness modify_put_get_correctness multi_thread multi_process put_cxl_page");
	printf("\n");
}

int parse(const char *s1, const char *s2)
{
	int start = strlen(s1) - 1;
	int end = 0;
	int unit = 0;
	char num_buf[128] = { '\0', };

	for (int i = start; i >= 0; i--) {
		if (s1[i] == 'b' || s1[i] == 'B') {
			if (i - 1 >= 0 && (s1[i - 1] == 'b' || s1[i - 1] == 'B'))
				return -1;
			continue;
		}

		if (s1[i] == 'g' || s1[i] == 'G')
			unit = 3;
		else if (s1[i] == 'm' || s1[i] == 'M')
			unit = 2;
		else if (s1[i] == 'k' || s1[i] == 'K')
			unit = 1;

		if (unit) {
			end = i;
			break;
		}
	}

	if (unit == 0 || end == 0)
		return -1;


	for (int i = 0; i < end; i++) {
		if (s1[i] < '0' || s1[i] > '9')
			return -1;
		num_buf[i] = s1[i];
	}
	size = atoi(num_buf);
	size <<= unit * 10;
	size = size / 4 * 4;

	if (size >= limit) {
		fprintf(stderr, "Test file size is over the limit\n");
		return -1;
	}

	for (int i = 0; i < TEST_NUM; i++) {
		if (!strcmp(s2, test_list[i].name)) {
			test_idx = i;
			test_func = test_list[test_idx].test_func;
			break;
		}
	}

	if (test_idx == -1)
		return -1;

	return 0;
}

void print_test_info()
{
	const char size_str = UNIT_STR(size);
	const char limit_str = UNIT_STR(limit);
	int size_bit = UNIT_BIT(size_str);
	int limit_bit = UNIT_BIT(limit_str);

	printf("======Test Info======\n");
	printf("Process ID : %d / CXL Cache Enabled : %c\n", pid, cxlcache_enabled ? 'Y' : 'N');
	printf("Test Name : %s\n", test_list[test_idx].name);
	printf("Test File Size %.2lf%c\n", (double)size / (1 << size_bit), size_str);
	printf("Test File Size Limit is %.2lf%c\n", (double)limit / (1 << limit_bit), limit_str); 
}

int get_env_info(const char *s)
{
	FILE *cmd;
	char result[1024];
	char operation[1024];

	sprintf(operation, "%s", s);
	cmd = popen(operation, "r");
	if (!cmd) {
		fprintf(stderr, "popen error");
		return -1;
	}

	int hit = 0;
	while (fgets(result, sizeof(result), cmd))
		hit = 1;

	pclose(cmd);

	return hit;
}

int get_is_system_has_cxlcache()
{
	const char *s = "grep -ie CONFIG_CXLCACHE=y /boot/config-$(uname -r)";
	return get_env_info(s);
}

int get_cxlcache_enable_status()
{
	const char *s = "grep -ie y /sys/module/cxlcache/parameters/enabled";
	return cxlcache_enabled = get_env_info(s);
}

int remove_file_if_exist(char *path)
{
	int ret = 0;

	if (access(path, F_OK) == -1)
		goto done;

	if (remove(path) != 0) {
		ret = ENV_SET_FAIL;
		fprintf(stderr, "test file can't remove\n");
	}

	sync();
done:
	return ret;
}

char *create_file(int file_id)
{
	char * path;
	int fd;
	int *mmap_buf;

	if ((path = (char *) calloc(PATH_MAX, sizeof(char))) == NULL) {
		fprintf(stderr, "fail calloc for path buf\n");
		goto fail;
	}

	strcpy(path, test_path);
	strcat(path, "/test");
	sprintf(path, "%s%d.dat", path, file_id);

	if (remove_file_if_exist(path) != 0)
		goto fail;

	fd = open(path, O_CREAT | O_SYNC | O_RDWR, 0666);
	if (fd == -1) {
		fprintf(stderr, "open test file fail\n");
		goto fail;
	}

	if (lseek(fd, size, SEEK_SET) == -1) {
		fprintf(stderr, "set opend file offset fail\n");
		goto fail;
	}

	if (write(fd, " ", 1) != 1) {
		fprintf(stderr, "write space to last byte of fd fail\n");
		goto fail;
	}

	mmap_buf = mmap(0, size + 4, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
	if (mmap_buf == NULL) {
		fprintf(stderr, "mmap test file fail\n");
		goto fail;
	}

	for (int i = 0; i < size / 4; i++) {
		mmap_buf[i] = i;
	}

	if (fsync(fd) == -1) {
		fprintf(stderr, "fsync for written file fail\n");
		goto fail;
	}

	if (munmap(mmap_buf, size + 4) == -1) {
		fprintf(stderr, "unmapping test file fail\n");
		goto fail;
	}

	if (close(fd) == -1) {
		fprintf(stderr, "close test file fail\n");
		goto fail;
	}

	return path;

fail:
	return NULL;
}

int read_file_seq(char *path)
{
	int fd;
	int *mmap_buf;

	fd = open(path, O_RDONLY, 0666);
	if (fd == -1) {
		fprintf(stderr, "open test file fail\n");
		goto fail;
	}

	mmap_buf = mmap(0, size + 4, PROT_READ, MAP_SHARED, fd , 0);
	if (mmap_buf == NULL) {
		fprintf(stderr, "mmap test file fail\n");
		goto fail;
	}

	for (int i = 0; i < size / 4; i++) {
		if (mmap_buf[i] != i) {
			fprintf(stderr, "value between put and get is different\n");
			return TEST_FAILURE;
		}
	}

	if (munmap(mmap_buf, size + 4) == -1) {
		fprintf(stderr, "unmapping test file fail\n");
		goto fail;
	}

	if (close(fd) == -1) {
		fprintf(stderr, "close test file fail\n");
		goto fail;
	}

	return TEST_SUCCESS;
fail:
	return TEST_FAILURE;
}

int read_file_rev(char *path)
{
	int fd;
	int *mmap_buf;

	fd = open(path, O_RDWR, 0666);
	if (fd == -1) {
		fprintf(stderr, "open test file fail\n");
		goto fail;
	}

	mmap_buf = mmap(0, size + 4, PROT_READ, MAP_SHARED, fd, 0);
	if (mmap_buf == NULL) {
		fprintf(stderr, "mmap test file fail\n");
		goto fail;
	}

	for (int i = 0; i < size / 4; i++) {
		if (mmap_buf[i] != (int)size / 4 - i) {
			fprintf(stderr, "value between put and get is different\n");
			return TEST_FAILURE;
		}
	}

	if (munmap(mmap_buf, size + 4) == -1) {
		fprintf(stderr, "unmapping test file fail\n");
		goto fail;
	}

	if (close(fd) == -1) {
		fprintf(stderr, "close test file fail\n");
		goto fail;
	}

	return TEST_SUCCESS;
fail:
	return TEST_FAILURE;
}

int write_file_seq(char *path)
{
	int fd;
	int *mmap_buf;

	fd = open(path, O_RDWR, 0666);
	if (fd == -1) {
		fprintf(stderr, "open test file fail\n");
		goto fail;
	}

	mmap_buf = mmap(0, size + 4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (mmap_buf == NULL) {
		fprintf(stderr, "mmap test file fail\n");
		goto fail;
	}

	for (int i = 0; i < size / 4; i++) {
		mmap_buf[i] = i;
	}

	if (fsync(fd) == -1) {
		fprintf(stderr, "fsync for written file fail\n");
		goto fail;
	}

	if (munmap(mmap_buf, size + 4) == -1) {
		fprintf(stderr, "unmapping test file fail\n");
		goto fail;
	}

	if (close(fd) == -1) {
		fprintf(stderr, "close test file fail\n");
		goto fail;
	}

	return TEST_SUCCESS;
fail:
	return TEST_FAILURE;
}

int write_file_rev(char *path)
{
	int fd;
	int *mmap_buf;

	fd = open(path, O_RDWR, 0666);
	if (fd == -1) {
		fprintf(stderr, "open test file fail\n");
		goto fail;
	}

	mmap_buf = mmap(0, size + 4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (mmap_buf == NULL) {
		fprintf(stderr, "mmap test file fail\n");
		goto fail;
	}

	for (int i = 0; i < size / 4; i++) {
		mmap_buf[i] = (int)size / 4 - i;
	}

	if (fsync(fd) == -1) {
		fprintf(stderr, "fsync for written file fail\n");
		goto fail;
	}

	if (munmap(mmap_buf, size + 4) == -1) {
		fprintf(stderr, "unmapping test file fail\n");
		goto fail;
	}

	if (close(fd) == -1) {
		fprintf(stderr, "close test file fail\n");
		goto fail;
	}

	return TEST_SUCCESS;
fail:
	return TEST_FAILURE;
}

int drop_caches(char val)
{
	char cmd[] = "echo i > /proc/sys/vm/drop_caches";
	int ret = 0;
	cmd[5] = val;
	if (system(cmd) < 0) {
		fprintf(stderr, "Page cache drop fail\n");
		ret = ENV_SET_FAIL;
	}
	return ret;
}

void *modify_put(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	int ret = 0;

	ret = read_file_seq(tdata->path);
	if (ret != 0)
		goto end;

	ret = write_file_rev(tdata->path);
	if (ret != 0)
		goto end;

	ret = drop_caches('1');
	if (ret != 0)
		goto end;

	ret = read_file_rev(tdata->path);
	if (ret != 0)
		goto end;

end:
	tdata->ret = ret;
	pthread_exit(NULL);
}

void result_summary(u64 put_pages, u64 get_pages)
{
	const char put_str = UNIT_STR(put_pages), get_str = UNIT_STR(get_pages);
	int put_bit = UNIT_BIT(put_str), get_bit = UNIT_BIT(get_str);

	printf("====== RESULT ======\n");
	printf("CXL Cache Succ Put Pages After Caching : %.2lf%c \n",
			(double)put_pages / (1 << put_bit), put_str);
	printf("CXL Cache Succ Get Pages After Caching : %.2lf%c \n",
			(double)get_pages / (1 << get_bit), get_str);
}

void get_succ_gets(u64 *val)
{
	int fd;
	char buf[256];
	int ret;

	fd = open("/sys/kernel/debug/cleancache/succ_gets", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open cleancache debugfs fail\n");
		*val = 0;
		return;
	}

	ret = read(fd, buf, 256);
	if (ret <= 0) {
		fprintf(stderr, "read cleancache debugfs fail\n");
		*val = 0;
		return;
	}

	buf[ret] = '\0';
	*val = atoi(buf);
}

void get_succ_puts(u64 *val)
{
	int fd;
	char buf[256];
	int ret;

	fd = open("/sys/kernel/debug/cleancache/succ_puts", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open cleancache debugfs fail\n");
		*val = 0;
		return;
	}

	ret = read(fd, buf, 256);
	if (ret <= 0) {
		fprintf(stderr, "read cleancache debugfs fail\n");
		*val = 0;
		return;
	}

	buf[ret] = '\0';
	*val = atoi(buf);
}
