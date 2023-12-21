#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <numa.h>
#include <numaif.h>
#include "common.h"

#define MAX_CHAR_LEN (255)

static int loop_count;
static int usec;
static int print_buddyinfo;

static struct bitmask *bmp_has_cpu, *bmp_has_cxl;

static int get_nodemask(void)
{
	int nid, max_node;
	char path[MAX_CHAR_LEN] = {0, };
	char has_cpu[MAX_CHAR_LEN] = {0, };
	char has_memory[MAX_CHAR_LEN] = {0, };
	FILE *file;

	sprintf(path, "/sys/devices/system/node/has_cpu");
	file = fopen(path, "r");
	if (!file) {
		printf("failed to open /sys/devices/system/node/has_cpu\n");
		return -1;
	}

	fgets(has_cpu, MAX_CHAR_LEN, file);
	strtok(has_cpu, "\n");
	fclose(file);

	sprintf(path, "/sys/devices/system/node/has_memory");
	file = fopen(path, "r");
	if (!file) {
		printf("failed to open /sys/devices/system/node/has_memory\n");
		return -1;
	}

	fgets(has_memory, MAX_CHAR_LEN, file);
	strtok(has_memory, "\n");
	fclose(file);

	bmp_has_cpu = numa_parse_nodestring(has_cpu);
	bmp_has_cxl = numa_parse_nodestring(has_memory);

	max_node = numa_max_node();
	for (nid = 0; nid <= max_node; nid++) {
		if (numa_bitmask_isbitset(bmp_has_cpu, nid))
			numa_bitmask_clearbit(bmp_has_cxl, nid);
	}

	return 0;
}

static void usage(void)
{
	fprintf(stderr, "Usage: 'test_mmap_cxl [MEMTYPE] [loop N] [usleep N(us)]', "
			"[MEMTYPE=e|E : CXL nodes, n|N : DDR nodes, "
			"otherwise : all nodes]\n");
}

int main(int argc, char* argv[]) {
	int count = 0, i;
	unsigned long size = 4 * 1024 * 1024;
	unsigned int flag = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_POPULATE;
	int mem_type_set = 0;

	if (get_nodemask()) {
		fprintf(stderr, "Failed to get CXL nodemask\n");
		exit(2);
	}

	for (i = 1; i < argc; i++) {
		if (*argv[i] == 'e' || *argv[i] == 'E') {
			if (numa_bitmask_weight(bmp_has_cxl) == 0) {
				fprintf(stderr, "No CXL nodes on system\n");
				exit(1);
			}

			numa_set_membind(bmp_has_cxl);
			fprintf(stderr, "Node binding to CXL nodes\n");
			mem_type_set++;
		} else if (*argv[i] == 'n' || *argv[i] == 'N') {
			numa_set_membind(bmp_has_cpu);
			fprintf(stderr, "Node binding to DDR nodes\n");
			mem_type_set++;
		} else if (!strcmp(argv[i], "loop")) {
			loop_count = (int)atoi(argv[++i]);
		} else if (!strcmp(argv[i], "usleep")) {
			usec = (int)atoi(argv[++i]);
		} else if (!strcmp(argv[i], "buddyinfo")) {
			print_buddyinfo = 1;
		} else {
			usage();
			exit(2);
		}
		if (mem_type_set > 1){
			fprintf(stderr, "Invald memory type option\n");
			usage();
			exit(2);
		}
	}

	if (print_buddyinfo)
		system("cat /proc/buddyinfo > buddyinfo_before.out");

	while (1) {
		char *addr = mmap(NULL, size, PROT_READ|PROT_WRITE, flag, -1, 0);
		char one,zero;
		if (addr == MAP_FAILED) {
			perror("mmap error");
			exit(1);
		}

		memset(addr, '1', size);
		one = *(addr + size / 2);
		memset(addr, '0', size);
		zero = *(addr + size / 2);
		printf("addr[%p], one='%c' zero='%c'\n", (void *)addr, one, zero);

		if(one != '1' || zero != '0'){
			perror("memset error");
			exit(1);
		}

#if 0
		if (munmap(tmp, 1024) == -1) {
			perror("munmap error");
			exit(1);
		}
#endif
		if (loop_count > 0 && ++count >= loop_count)
			break;

		if (usec > 0)
			usleep(usec);
		else
			sleep(1);
	}

	if (print_buddyinfo)
		system("cat /proc/buddyinfo > buddyinfo_after.out");

	return 0;
}

