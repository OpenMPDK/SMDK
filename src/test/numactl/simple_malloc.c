#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(void)
{
	unsigned long size = 4 * 1024 * 1024;

	while (1) {
		char *addr = malloc(size);
		char one,zero;

		if (!addr) {
			perror("malloc error");
			exit(1);
		}

		memset(addr, '1', size);
		one = *(addr + size / 2);
		memset(addr, '0', size);
		zero = *(addr + size / 2);
		printf("addr[%p], one='%c' zero='%c'\n", (void *)addr, one, zero);

		sleep(1);
	}

	return 0;
}
