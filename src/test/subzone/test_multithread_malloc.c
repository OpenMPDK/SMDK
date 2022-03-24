#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/time.h>

#define USE_MALLOC

#define array_len(x) (sizeof(x)/sizeof(*(x)))

struct option opts[] = {
	{ "size", 1, 0, 's' },
	{ "iter", 1, 0, 'i' },
	{ "thread", 1, 0, 'c' },
	{ 0 }
};

#define MAX_NR_THREADS	50

unsigned long size = 64 * 1024;
unsigned int iter = 10;
unsigned int nthread = 1;
#ifndef USE_MALLOC
unsigned int flag = MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_POPULATE;
#endif

void *thread_main(void *arg)
{
	char *addr;
	unsigned int i;

	assert(arg);

	for (i = 0; i < iter; i++) {
#ifdef USE_MALLOC
		addr = malloc(size);
		assert(addr);
		memset(addr, 0x0, size);
#else
		addr = mmap(NULL, size, PROT_READ|PROT_WRITE, flag, -1, 0);
		assert(addr != MAP_FAILED);
#endif
		// free(addr) or munmap(addr, size)
	}
	return NULL;
}

void do_test(void)
{
	pthread_t threads[MAX_NR_THREADS];
	unsigned int i;

	for (i = 0; i < nthread; i++) {
		pthread_create(&threads[i], NULL, thread_main, (void *)&i);
		usleep(1000);
	}

	for (i = 0; i < nthread; i++) {
		pthread_join(threads[i], NULL);
	}
}

int main(int argc, char *argv[])
{
	int c;
	char shortopts[array_len(opts)*2 + 1];

	while ((c = getopt_long(argc, argv, shortopts, opts, NULL)) != -1) {
		switch (c) {
			case 's':
				size = (unsigned long)atoi(optarg);
				break;
			case 'i':
				iter = (unsigned int)atoi(optarg);
				break;
			case 'c':
				nthread = (unsigned int)atoi(optarg);
				break;
			default:
				break;
		}
	}

	do_test();

	return 0;
}
