#ifndef __CHECK_LAZY_ALLOC_H_
#define __CHECK_LAZY_ALLOC_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include <getopt.h>
#include <ctype.h>

#include <sys/sysinfo.h>
#include <sys/wait.h>

#include <pthread.h>

#include <common.h>

#define NAME_NORMAL_ZONE "Normal"
#define NAME_EX_ZONE "ExMem"

#define MAX_CHAR_LEN (255)
//TODO: need to increase
#define MAX_NR_THREADS (2)
#define NR_THREADS	(15)

#define GB_SIZE (1024 * 1024 * 1024)
#define PG_SIZE (4 * 1024)

#define ARR_SIZE (1024 * 1024)
#define GRANUALITY_SIZE (1024 * 1024)


#define TC_PASS (0)
#define TC_FAIL (1)
#define TC_ERROR (-1)

#define PG_JUMP (256)
#define G_JUMP (32)

typedef enum {
	mem_zone_normal=0,		/* point NORMAL_ZONE */
	mem_zone_ex,			/* point CXL_ZONE */
	mem_zone_invalid		/* point invalid type */
} mem_zone_t;

typedef enum {
	before_mmap = 0,
	after_mmap,
	after_write,
	after_munmap,
	max_phase
}phase_t;

typedef struct _test {
	int test_id;
	int (*test_func)(unsigned int, char *);
	unsigned int mmap_flag;
	char *test_name;
	char *test_desc;
}test;

#endif 
