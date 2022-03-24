#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>


int main(void) {
	unsigned long size = 4*1024*1024;
	unsigned int flag = MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_POPULATE;
	struct timeval startTime, endTime, gapTime;
	unsigned int elapsedTime;
	int i = 0;

	gettimeofday(&startTime, NULL);

	for (i = 0 ; i < 100; i++) {
		char *addr = mmap(NULL, size, PROT_READ|PROT_WRITE, flag, -1, 0);
		if (addr == MAP_FAILED) {
			perror("mmap error");
			exit(1);
		}
		char one,zero;
		memset(addr,'1',size);
		one=*(addr+size/2);
		memset(addr,'0',size);
		zero=*(addr+size/2);
		printf("addr[%p], one='%c' zero='%c'\n", (void *)addr, one, zero);
		sleep(1);
	}

	gettimeofday(&endTime, NULL);
	gapTime.tv_sec = endTime.tv_sec - startTime.tv_sec;
	gapTime.tv_usec = endTime.tv_usec - startTime.tv_usec;
	elapsedTime = gapTime.tv_sec*1000000 + gapTime.tv_usec;
	printf("done: elapsed time(us): %u\n", elapsedTime);

	return 0;
}

