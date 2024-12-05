#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/mman.h>

#define USE_MALLOC
#define MAX_UNIT	(128 * 1024) // 128KB
#define MAX_SIZE	((unsigned long)4096 * 1024 * 1024) // 4GB

int main(void)
{
	unsigned long size = 0, total_size = 0;
	char *addr;

	srand(time(NULL));

	while (1) {
		size = rand() % MAX_UNIT;
#ifdef USE_MALLOC
		addr = malloc(size);
		assert(addr);
#else
		addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		assert(addr != MAP_FAILED);
#endif
		memset(addr, 0x0, size);
		// free(addr) or munmap(addr, size)
		total_size += size;
		if (total_size >= MAX_SIZE)
			break;
	}

	printf("Alloction size: %ld\n", total_size);
	sleep(10);

	return 0;
}
