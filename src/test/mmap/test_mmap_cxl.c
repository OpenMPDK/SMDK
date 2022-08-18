#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "common.h"

static int loop_count;
static int usec;
static int print_buddyinfo;

int main(int argc, char* argv[]) {
	int count = 0, i;
	unsigned long size = 4 * 1024 * 1024;
	unsigned int flag = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_POPULATE;

	for (i = 1; i < argc; i++) {
		if (*argv[i] == 'e' || *argv[i] == 'E') {
			flag |= MAP_EXMEM;
			fprintf(stderr, "MAP_EXMEM\n");
		} else if (*argv[i] == 'n' || *argv[i] == 'N') {
			flag |= MAP_NORMAL;
			fprintf(stderr, "MAP_NORMAL\n");
		} else if (!strcmp(argv[i], "loop")) {
			loop_count = (int)atoi(argv[++i]);
		} else if (!strcmp(argv[i], "usleep")) {
			usec = (int)atoi(argv[++i]);
		} else if (!strcmp(argv[i], "buddyinfo")) {
			print_buddyinfo = 1;
		} else {
			fprintf(stderr, "Usage: 'test_mmap_cxl [ZONE] [loop N] [usleep N(us)]', "
					"[ZONE=e|E : ZONE_EXMEM, n|N : Excluding ZONE_EXMEM, "
					"otherwise : all zones]\n");
			exit(1);
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

