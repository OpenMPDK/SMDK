#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <numa.h>

#include "common.h"

#define MAX_CHAR_LEN (255)

static struct bitmask *bmp_has_cpu, *bmp_has_cxl;

int get_nodemask(void)
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

int main() {
	size_t size = 4*1024*1024*1024ull;
	unsigned int flag = MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_POPULATE;

	if (get_nodemask()) {
		fprintf(stderr, "Failed to get CXL nodemask\n");
		exit(1);
	}

	if (numa_bitmask_weight(bmp_has_cxl) == 0) {
		fprintf(stderr, "No CXL nodes on system\n");
		exit(1);
	}

	numa_set_membind(bmp_has_cxl);

	char *addr = mmap(NULL, size, PROT_READ|PROT_WRITE, flag, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap error");
		exit(1);
	}

	printf("addr[%p]\n", (void *)addr);


	return 0;
}

