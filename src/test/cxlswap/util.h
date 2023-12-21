#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <omp.h>
#include <unistd.h>

extern int errno;
#define ENV_SET_FAIL (2)
#define TEST_FAILURE (1)
#define TEST_SUCCESS (0)

#define INPUT_ERR(s) { input_help_message(s); return 2; }

#define PROT (PROT_READ|PROT_WRITE)
#define FLAG (MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE)

#define TMPFILE ".tmp_test_cxlswap_store"
#define CGROUP_ROOT "/sys/fs/cgroup"
#define CGROUP_V1_CHECK "/sys/fs/cgroup/memory/memory.limit_in_bytes"
#define CGROUP_V2_CHECK "/sys/fs/cgroup/cgroup.subtree_control"
#define CXLSWAP_SYSFS "/sys/kernel/debug/cxlswap"
#define CXLSWAP_STORED_PAGES_PATH CXLSWAP_SYSFS"/stored_pages"
#define CXLSWAP_SAME_FILLED_PAGES_PATH CXLSWAP_SYSFS"/same_filled_pages"

#define GB (1 << 30L)
#define MB (1 << 20L)
#define KB (1 << 10L)
#define UNIT_STR(u) (u >= GB) ? 'G' : (u >= MB) ? 'M' : 'K'
#define UNIT_BIT(u) (u == 'G') ? 30 : (u == 'M') ? 20 : 10

#define BUF_SIZE 128

#define TEST_NUM 3

typedef uint64_t u64;
typedef struct __test_info {
	int (*test_func)();
	const char *name;
} Test_Info;

int (*test_func)();
int simple_store();
int store_load();
int multi_thread();
int shared_memory();

Test_Info test_list[TEST_NUM] = {
	{ &store_load, "store_load" },
	{ &multi_thread, "multi_thread" },
	{ &shared_memory, "shared_memory" }
};

pid_t pid;

int num_threads;
int test_idx = -1;
int cgroup_version;
int cxlswap_enabled;

char cgroup_child[BUF_SIZE];

u64 size;
u64 limit;

void input_help_message(const char *s)
{
    printf("\n");
    printf("Usage : %s size limit test\n\n", s);
    printf("e.g. %s 10M 60 store_load\n", s);
    printf("   Total Size 10M but limit to 60%%(6M) -> 4M maybe swapout");
	printf("\n\n");
    printf("e.g. %s 10G 60 multi_thread 10\n", s);
    printf("   Same with above but 10 threads occur swap in/out simultanously");
	printf("\n\n");
    printf("e.g. %s 1g 80 shared_memory\n", s);
    printf("   Parent Process allocate 1G memory, child process access that\n");
    printf("   Swap out/in occurs both parent and child \n\n");
	printf("\n\n");
	printf("Support test list below\n");
	printf("store_load multi_thread shared_memory\n");
    printf("\n");
}

void cgroup_help_message()
{
    printf("\n");
    printf("Cgroup config on but not mounted for this test\n");
    printf("If you want to test using cgroup-v1, try under command\n");
    printf("mount -t tmpfs cgroup_root /sys/fs/cgroup\n");
    printf("mkdir /sys/fs/cgroup/memory\n");
    printf("mount -t cgroup memory -o memory /sys/fs/cgroup/memory\n");
    printf("\n");
    printf("If you want to test using cgroup-v2, try under command\n");
    printf("mount -t cgroup2 cgroup_root /sys/fs/cgroup\n");
    printf("echo \"+memory\" > /sys/fs/cgroup/cgroup.subtree_control\n");
    printf("\n");
}

void print_test_info()
{
    const char size_str = UNIT_STR(size);
    const char limit_str = UNIT_STR(limit);
    int size_bit = UNIT_BIT(size_str);
    int limit_bit = UNIT_BIT(limit_str);
	printf("=======Test Info======\n");
    printf("Process ID : %d / CXL Swap Enabled : %c\n", 
					pid, cxlswap_enabled ? 'Y' : 'N');
    printf("Test Name : %s\n", test_list[test_idx].name);
    printf("Total Memory Size %.2lf%c / ", 
									(double)size / (1 << size_bit), size_str);
    printf("Memory Limit to %.2lf%c\n", 
									(double)limit / (1 << limit_bit), limit_str);
}

void set_err(const char *s, int ret)
{
    errno = WEXITSTATUS(ret);
    perror(s);
}

int get_env_info(const char *s)
{
	FILE *cmd;
	char result[1024];
	char operation[BUF_SIZE];
	
	sprintf(operation, "%s", s);
	cmd = popen(operation, "r");
	
	int hit = 0;
	while (fgets(result, sizeof(result), cmd)) 
		hit = 1;
	
	pclose(cmd);

	return hit;
}

int get_is_system_has_cxlswap() 
{
	const char *s = "grep -ie CONFIG_CXLSWAP=y /boot/config-$(uname -r)";
	return get_env_info(s);
}

int get_is_system_has_cgroup()
{
	const char *s = "grep -ie CONFIG_CGROUPS=y /boot/config-$(uname -r)";
	return get_env_info(s);
}

int get_cxlswap_enable_status()
{
    const char *s = "grep -ie y /sys/module/cxlswap/parameters/enabled";
	return cxlswap_enabled = get_env_info(s);
}

int get_cgroup_version()
{
	int ret;
	const char *s = "grep -ioe cgroup_hierarchy=1 -ioe cgroup_no_v1=memory -ioe cgroup_no_v1=all /proc/cmdline";

	if (access(CGROUP_V1_CHECK, F_OK) != -1)
		cgroup_version = 1;
	else if (access(CGROUP_V2_CHECK, F_OK) != -1)
		cgroup_version = 2;
	else if (get_env_info(s)) {
		if ((ret = system("mount -t cgroup2 cgroup_root /sys/fs/cgroup"))) {
        	set_err("mount cgroup2 on /sys/fs/cgroup failed", ret);
			return -1;
		}

		if ((ret = system("echo '+memory' > /sys/fs/cgroup/cgroup.subtree_control"))) {
        	set_err("set memory controller on cgroup v2 failed", ret);
			return -1;
		}

		cgroup_version = 2;
	}

	return cgroup_version;
}

int avoid_oom()
{
    int ret;
    char command[BUF_SIZE];

    sprintf(command, "echo -17 > /proc/%d/oom_score_adj", pid);
    if ((ret = system(command)) < 0) {
        set_err("Set oom_score_adj failed", ret);
        return -1;
    }

    return 0;
}

int create_cgroup()
{
    int ret;
    const char *s;
    char command[BUF_SIZE + 64];

    s = cgroup_version == 1 ? "memory/" : "/";
    sprintf(cgroup_child, "%s/%stest_cxlswap_%d", CGROUP_ROOT, s, pid);
    if (mkdir(cgroup_child, 0777) < 0) {
        perror("mkdir");
        return -1;
    }

    sprintf(command, "echo %d > %s/cgroup.procs", pid, cgroup_child);
    if ((ret = system(command))) {
        set_err("Attach process to cgroup failed", ret);
        return -1;
    }

    return 0;
}

int limit_memory()
{
    int ret;
    const char *s;
    char limits[BUF_SIZE + 64];
    char command[BUF_SIZE + 128];

    s = cgroup_version == 1 ? "limit_in_bytes" : "max";
    sprintf(limits, "%s/memory.%s", cgroup_child, s);

    sprintf(command, "echo %ld > %s", limit, limits);
    if ((ret = system(command))) {
        set_err("Error by try to limit memory lower than current usage", ret);
        return -1;
    }

    return 0;
}

int clean_env()
{
    int fd, retry = 0;
    size_t ret;
    const char *s;
    char tasks[BUF_SIZE];
    char pid_str[BUF_SIZE];

    s = cgroup_version == 1 ? "memory/tasks" : "cgroup.procs";
    sprintf(tasks, "%s/%s", CGROUP_ROOT, s);

    fd  = open(tasks, O_APPEND|O_WRONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    sprintf(pid_str, "%d", pid);
    ret = write(fd, pid_str, strlen(pid_str));
    if (ret != strlen(pid_str)) {
        perror("write");
        return -1;
    }
    close(fd);

	do {
		rmdir(cgroup_child);
		retry++;
		if (retry > 256)
			break;
	} while (access(cgroup_child, F_OK) == 0);

	if (retry > 256) 
		return -1;

	if (cgroup_version == 2) {
		if ((ret = system("echo '-memory' > /sys/fs/cgroup/cgroup.subtree_control"))) {
        	set_err("unset memory controller on cgroup v2 failed", ret);
			return -1;
		}

		if ((ret = system("umount /sys/fs/cgroup"))) {
        	set_err("umount cgroup2 on /sys/fs/cgroup failed", ret);
			return -1;
		}
	}
		
    return 0;
}

int parse(const char *s1, const char *s2, const char *s3)
{
    int start = strlen(s1) - 1;
    int end = 0;
    int unit = 0;
	int l;
    char num_buf[BUF_SIZE] = { '\0', };

    for(int i=start; i >= 0; i--) {
        if (s1[i] == 'b' || s1[i] == 'B') {
            if(i - 1 >= 0 && (s1[i - 1] == 'b' || s1[i - 1] == 'B'))
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

    for(int i=0;i<end;i++) {
        if (s1[i] < '0' || s1[i] > '9')
            return -1;
        num_buf[i] = s1[i];
    }
    size = atoi(num_buf);
    size <<= unit*10;

	end = strlen(s2);	
	for (int i=0 ;i<end; i++) 
		if ('0' > s2[i] || s2[i] > '9')
			return -1;
	
	l = atoi(s2);
    if (l <= 0 || l > 100)
        return -1;

    limit = l == 100 ? size : (u64)(((double)size / 100.0f)* atoi(s2));
	
	for (int i=0;i<TEST_NUM;i++) {
		if (!strcmp(s3, test_list[i].name)) {
			test_idx = i;
			test_func = test_list[test_idx].test_func;
			break;
		}
	}
	
	if (test_idx == -1)
		return -1;

    return 0;
}

void get_stored_pages(int *val)
{
    int fd;
    char buf[BUF_SIZE];
    int ret;

    fd = open(CXLSWAP_STORED_PAGES_PATH, O_RDONLY);
    if (fd < 0) {
        *val = 0;
        return;
    }
    ret = read(fd, buf, BUF_SIZE);
    if (ret <= 0) {
        *val = 0;
        return;
    }

    buf[ret] = '\0';
    *val = atoi(buf);

    fd = open(CXLSWAP_SAME_FILLED_PAGES_PATH, O_RDONLY);
    if (fd < 0) {
        *val = 0;
        return;
    }
    ret = read(fd, buf, BUF_SIZE);
    if (ret <= 0) {
        *val = 0;
        return;
    }
    buf[ret] = '\0';
    *val -= atoi(buf);
}
