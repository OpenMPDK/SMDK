/*
 * SPDX-License-Identifier: GPL-2.0
 * Notice: The following functions and logic for memory latency checking were written
 * by referring to the source codes from https://github.com/torvalds/test-tlb.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <sched.h>
#include <numa.h>

#define MAP_EXMEM (0x200000)
#define MAP_NORMAL (0x400000)

void alarm_handler(int sig);
double get_read_lat_by_nodes(int node_cpu, int node_mem, unsigned long size,
			     unsigned long stride, int random_access,
			     int num_iter);

static volatile int stop = 0;

static void *create_test_map(void *map, unsigned long size,
			     unsigned long stride, int node_mem)
{
	unsigned int flag =
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_POPULATE;
	unsigned long off;
	unsigned int *lastpos;
	struct bitmask *mem_mask_orig, *mem_mask;

	mem_mask_orig = numa_get_membind();
	mem_mask = numa_allocate_nodemask();
	numa_bitmask_setbit(mem_mask, node_mem);

	numa_set_membind(mem_mask);
	map = mmap(map, size, PROT_READ | PROT_WRITE, flag, -1, 0);
	numa_set_membind(mem_mask_orig);
	numa_free_nodemask(mem_mask);

	if (map == MAP_FAILED)
		return NULL;

	madvise(map, size, MADV_NOHUGEPAGE);

	lastpos = map;
	for (off = 0; off < size; off += stride) {
		lastpos = map + off;
		*lastpos = off + stride;
	}
	*lastpos = 0;

	return map;
}

static void randomize_test_map(void *map, unsigned long size,
			       unsigned long stride)
{
	unsigned long off;
	unsigned int *lastpos, *random_map;
	int n;

	srandom(time(NULL));

	random_map = calloc(size / stride + 1, sizeof(unsigned int));
	if (!random_map) {
		fprintf(stderr, "cannot allocte memory for randomization\n");
		return;
	}

	for (n = 0, off = 0; off < size; n++, off += stride)
		random_map[n] = off;

	for (n = 0, off = 0; off < size; n++, off += stride) {
		unsigned int m = (unsigned int)(rand() % (size / stride));
		unsigned int tmp = random_map[n];
		random_map[n] = random_map[m];
		random_map[m] = tmp;
	}

	lastpos = map;
	for (n = 0, off = 0; off < size; n++, off += stride) {
		lastpos = map + random_map[n];
		*lastpos = random_map[n + 1];
	}
	*lastpos = random_map[0];

	free(random_map);
}

static unsigned long usec_diff(struct timeval *a, struct timeval *b)
{
	unsigned long usec;

	usec = (b->tv_sec - a->tv_sec) * 1000000;
	usec += b->tv_usec - a->tv_usec;
	return usec;
}

void alarm_handler(int sig)
{
	stop = 1;
}

static double do_test(void *map, unsigned long testtime_sec)
{
	unsigned long count = 0, off = 0, usec;
	struct timeval start, end;
	struct itimerval itval = {
		.it_interval = { 0, 0 },
		.it_value = { 0, 0 },
	};

	usec = testtime_sec * 1000000;
	itval.it_value.tv_sec = usec / 1000000;
	itval.it_value.tv_usec = usec % 1000000;

	stop = 0;
	signal(SIGALRM, alarm_handler);
	setitimer(ITIMER_REAL, &itval, NULL);

	gettimeofday(&start, NULL);
	do {
		count++;
		off = *(unsigned int *)(map + off);
	} while (!stop);
	gettimeofday(&end, NULL);
	usec = usec_diff(&start, &end);

	*(volatile unsigned int *)(map + off);

	return 1000 * (double)usec / count;
}

double get_read_lat_by_nodes(int node_cpu, int node_mem, unsigned long size,
			     unsigned long stride, int random_access,
			     int num_iter)
{
	int i;
	unsigned long testtime_sec = 5;
	double ret = 50000.f;

	struct bitmask *cpu_mask = numa_allocate_nodemask();
	numa_bitmask_setbit(cpu_mask, node_cpu);
	numa_run_on_node_mask(cpu_mask);
	for (i = 0; i < num_iter; i++) {
		double d;
		void *buf = NULL;
		buf = create_test_map(buf, size, stride, node_mem);
		if (buf == NULL) {
			fprintf(stderr, "cannot allocate memory for test\n");
			return 0.f;
		}

		if (random_access)
			randomize_test_map(buf, size, stride);

		d = do_test(buf, testtime_sec);
		if (d < ret)
			ret = d;

		munmap(buf, size);
	}
	numa_free_nodemask(cpu_mask);

	return ret;
}
