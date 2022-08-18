#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <numa.h>

#include "common.h"

int main() {
	size_t size = 4*1024*1024*1024ull;
	unsigned int flag = MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_POPULATE|MAP_EXMEM;

	char *addr = mmap(NULL, size, PROT_READ|PROT_WRITE, flag, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap error");
		exit(1);
	}

	printf("addr[%p]\n", (void *)addr);


	return 0;
}

