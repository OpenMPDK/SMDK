#define _GNU_SOURCE
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include "test.h"
#include "smdk_opt_api.h"

#define MAX_NUM_TESTS (100)
#define MAX_NUM_THREADS (100)
#define ITER_DEFAULT (10)
#define NTHREADS_DEFAULT (1)
#define NSIZES_DEFAULT (1)

#define JEMALLOC_SC_SMALL_MAXCLASS (14*1024)
#define JEMALLOC_TCACHE_MAXCLASS (32*1024)

#define COLOR_RED "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_RESET "\x1b[0m"

#define _GET_TIME(t) gettimeofday((t), NULL);

#define GET_TIME_START(p) do {\
		if ((p)->is_time_needed) _GET_TIME(&((p)->start_time));\
	}while(0);

#define GET_TIME_END(p) do {\
		if ((p)->is_time_needed) _GET_TIME(&((p)->end_time));\
	}while(0);

#define PRINT_ELAPSED_TIME(p) do {\
		if ((p)->is_time_needed) {\
			unsigned int elapsed = \
				(((p)->end_time).tv_sec - ((p)->start_time).tv_sec) * 1000000 + (((p)->end_time).tv_usec - ((p)->start_time).tv_usec);\
			printf("Elapsed time(us): %u\n", elapsed);\
		}\
	} while(0);

#define PRINT_TEST(i, t, c, s) do {\
		printf(c"[Test%2d(tid=%d)] %s"COLOR_RESET"\n", i, t, s);\
	}while(0);

typedef void* (*test_func_t)(void*);

typedef struct {
	test_func_t f_test;
	char* desc_test;
} test_t;

typedef struct {
	unsigned int nsizes;
	unsigned int iter;
	smdk_memtype_t type;
	bool is_time_needed;
	struct timeval start_time;
	struct timeval end_time;
} test_param_t;

size_t size_arr[] = {8, 512, 4*1024, 16*1024, 64*1024};

void* test1(void* arg) {
	printf("Note: Test option 'iter', 'vsizes' would be ignored\n");

	test_param_t* test_param = (test_param_t*)arg;
	size_t size = size_arr[0];
	smdk_memtype_t type = test_param->type;
	void* malloc_buf, *calloc_buf, *posix_memalign_buf;

	malloc_buf = s_malloc(type, size);
	assert_ptr_not_null(malloc_buf, "s_malloc allocation failure");
	s_free_type(type, malloc_buf);

	calloc_buf = s_calloc(type, 1, size);
	assert_ptr_not_null(calloc_buf, "s_calloc allocation failure");

	calloc_buf = s_realloc(type, calloc_buf, size*2);
	assert_ptr_not_null(calloc_buf, "s_realloc allocation failure");
	s_free_type(type, calloc_buf);

	int ret = s_posix_memalign(type, &posix_memalign_buf, sizeof(void*), size);
	assert_d_eq(ret, 0, "s_posix_memalign allocation failure");
	s_free_type(type, posix_memalign_buf);

	return NULL;
}

void* test2(void* arg) {
	test_param_t* test_param = (test_param_t*)arg;
	size_t size;
	void* buf;

	for(unsigned int i = 0; i < test_param->iter; i++){
		size = size_arr[i % test_param->nsizes];
		buf = s_malloc(test_param->type, size);
		assert_ptr_not_null(buf, "s_malloc allocation failure");
		memset(buf, '0', size);
		s_free_type(test_param->type, buf);
	}

	return NULL;
}

void* test3(void* arg) {
	test_param_t* test_param = (test_param_t*)arg;
	size_t size;
	void* buf;

	for(unsigned int i = 0; i < test_param->iter; i++){
		size = size_arr[i % test_param->nsizes];
		buf = s_malloc(test_param->type, size);
		assert_ptr_not_null(buf, "s_malloc allocation failure");
		memset(buf, '0', size);
		s_free_type(test_param->type, buf);
		test_param->type = !(test_param->type);
	}

	return NULL;
}

void* test4(void* arg) {
	test_param_t* test_param = (test_param_t*)arg;
	size_t size;
	void* buf;
	smdk_memtype_t type;

	srand(time(NULL));
	for(unsigned int i = 0; i < test_param->iter; i++){
		size = size_arr[i % test_param->nsizes];
		type = rand() % 2; //0: Normal, 1: ExMem
		buf = s_malloc(type, size);
		assert_ptr_not_null(buf, "s_malloc allocation failure");
		memset(buf, '0', size);
		s_free_type(type, buf);
	}

	return NULL;
}

void* test5(void* arg) {
	printf("Note: Test option 'exmem' would be ignored\n");

	test_param_t* test_param = (test_param_t*)arg;

	size_t size = size_arr[0];
	void* buf, *buf2, *buf3;
	int ret;
	if (size > JEMALLOC_TCACHE_MAXCLASS) {
		printf("Note: 'size' should be smaller than %uB for realloc tests\n", JEMALLOC_TCACHE_MAXCLASS);
		printf("       force reset the size to %uB...\n", JEMALLOC_TCACHE_MAXCLASS);
		size = JEMALLOC_TCACHE_MAXCLASS;
	}

	/*case 1: invalid memtype*/
	buf = s_malloc(SMDK_MEM_NORMAL-1, size);
	assert_ptr_null(buf, "s_malloc wrong mem type checking error");
	buf = s_malloc(SMDK_MEM_EXMEM+1, size);
	assert_ptr_null(buf, "s_malloc wrong mem type checking error");

	buf = s_calloc(SMDK_MEM_NORMAL-1, 1, size);
	assert_ptr_null(buf, "s_calloc wrong mem type checking error");
	buf = s_calloc(SMDK_MEM_EXMEM+1, 1, size);
	assert_ptr_null(buf, "s_calloc wrong mem type checking error");

	buf = s_realloc(SMDK_MEM_NORMAL-1, buf, size);
	assert_ptr_null(buf, "s_realloc wrong mem type checking error");
	buf = s_realloc(SMDK_MEM_EXMEM+1, buf, size);
	assert_ptr_null(buf, "s_realloc wrong mem type checking error");

	ret = s_posix_memalign(SMDK_MEM_NORMAL-1, &buf, sizeof(void*), size);
	assert_d_ne(ret, 0, "s_posix_memalign wrong mem type checking error");
	ret = s_posix_memalign(SMDK_MEM_EXMEM+1, &buf, sizeof(void*), size);
	assert_d_ne(ret, 0, "s_posix_memalign wrong mem type checking error");

	/*case 2: realloc memtype mismatch*/
	buf = s_malloc(SMDK_MEM_NORMAL, size);
	assert_ptr_not_null(buf, "s_malloc allocation failrue");
	buf2 = s_realloc(SMDK_MEM_EXMEM, buf, size); //realloc request w/ different memtype
	assert_ptr_not_null(buf2, "s_realloc allocation failrue");
	buf3 = s_malloc(SMDK_MEM_NORMAL, size);
	assert_ptr_not_null(buf3, "s_malloc allocation failure");
	assert_ptr_eq(buf, buf3, "s_realloc failure: old_ptr has not been freed");
	s_free_type(SMDK_MEM_NORMAL, buf);
	s_free_type(SMDK_MEM_EXMEM, buf2);

	/*case 3: realloc memtype mismatch with size=0*/
	buf = s_malloc(SMDK_MEM_NORMAL, size);
	assert_ptr_not_null(buf, "s_malloc allocation failrue");
	buf2 = s_realloc(SMDK_MEM_EXMEM, buf, 0); //realloc request w/ different memtype and 0 size
	assert_ptr_null(buf2, "s_realloc 0-allocation failrue");
	buf3 = s_malloc(SMDK_MEM_NORMAL, size);
	assert_ptr_not_null(buf3, "s_malloc allocation failure");
	assert_ptr_eq(buf, buf3, "s_realloc failure: old_ptr has not been freed");
	s_free_type(SMDK_MEM_NORMAL, buf3);

	/*case 4: free memtype mismatch*/
	buf = s_malloc(SMDK_MEM_NORMAL, size);
	assert_ptr_not_null(buf, "s_malloc allocation failrue");
	memset(buf, '0', size);
	s_free_type(SMDK_MEM_EXMEM, buf); //wrong mem type
	buf2 = s_malloc(SMDK_MEM_NORMAL, size);
	assert_ptr_not_null(buf2, "s_malloc allocation failure");
	memset(buf2, '0', size);
	assert_ptr_eq(buf, buf2, "buffer with wrong type was not freed");
	s_free_type(SMDK_MEM_NORMAL, buf2);

	return NULL;
}

void* test6(void* arg) {
	test_param_t* test_param = (test_param_t*)arg;
	size_t size, realloc_size;
	void* buf, *buf_new, *realloc_buf;

	for(unsigned int i = 0; i < test_param->iter; i++){
		size = size_arr[i % test_param->nsizes];
		buf = s_malloc(test_param->type, size);
		assert_ptr_not_null(buf, "s_malloc allocation failure");
		memset(buf, '0', size);

		realloc_size = size; //size * 2, size / 2, ...
		realloc_buf = s_realloc(!(test_param->type), buf, realloc_size); //memtype change
		assert_ptr_not_null(realloc_buf, "s_realloc allocation failure");
		assert_c_eq(((char*)realloc_buf)[0], '0', "s_realloc did not copy old data");

		buf_new = s_malloc(test_param->type, size);
		assert_ptr_eq(buf, buf_new, "the old buffer in realloc has not been freed");

		s_free_type(test_param->type, buf_new);
		s_free_type(!(test_param->type), realloc_buf);
		test_param->type = !(test_param->type);
	}

    return NULL;
}

void* test7(void* arg) {
	test_param_t* test_param = (test_param_t*)arg;
	size_t size;
	void* buf;

	for(unsigned int i = 0; i < test_param->iter; i++){
		size = size_arr[i % test_param->nsizes];
		buf = s_malloc(test_param->type, size);
		assert_ptr_not_null(buf, "s_malloc allocation failure");
		memset(buf, '0', size);
		s_free_type(!(test_param->type), buf);
	}

	return NULL;
}

void* test8(void* arg) {
	test_param_t* test_param = (test_param_t*)arg;
	size_t size, mem_used_normal, mem_used_exmem;
	smdk_memtype_t type;

	void** buf = malloc(sizeof(void*) * test_param->iter);
	assert_ptr_not_null(buf, "malloc allocation failure");

	mem_used_normal = s_get_memsize_used(SMDK_MEM_NORMAL);
	mem_used_exmem = s_get_memsize_used(SMDK_MEM_EXMEM);
	printf("mem_used_normal(before malloc: %zu\n", mem_used_normal);
	printf("mem_used_exmem(before malloc): %zu\n", mem_used_exmem);

	srand(time(NULL));
	for(unsigned int i = 0; i < test_param->iter; i++){
		size = size_arr[i % test_param->nsizes];
		type = rand() % 2; //0: Normal, 1: ExMem
		buf[i] = s_malloc(type, size);
		assert_ptr_not_null(buf[i], "s_malloc allocation failure");
		memset(buf[i], '0', size);
	}
	mem_used_normal = s_get_memsize_used(SMDK_MEM_NORMAL);
	mem_used_exmem = s_get_memsize_used(SMDK_MEM_EXMEM);
	printf("mem_used_normal(after malloc): %zu\n", mem_used_normal);
	printf("mem_used_exmem(after malloc): %zu\n", mem_used_exmem);

	for(unsigned int i = 0; i < test_param->iter; i++){
		s_free(buf[i]);
	}
	mem_used_normal = s_get_memsize_used(SMDK_MEM_NORMAL);
	mem_used_exmem = s_get_memsize_used(SMDK_MEM_EXMEM);
	printf("mem_used_normal(after free): %zu\n", mem_used_normal);
	printf("mem_used_exmem(after free): %zu\n", mem_used_exmem);

	return NULL;
}

test_t arr_test[] = {
	{test1, "Basic optimization API running test"},
	{test2, "Multiple s_malloc and s_free_type requests with specified mem type (default: normal)"},
	{test3, "Multiple s_malloc requests alternating two memory types and s_free_type"},
	{test4, "Multiple s_malloc requests with random memory types and s_free_type"},
	{test5, "Memtype exception cases"},
	{test6, "Multiple s_realloc requests with different memtype than old ptr"},
	{test7, "Multiple s_malloc and s_free_type requests with different memtype"},
	{test8, "Multiple s_malloc requests with random memory types and s_free"},
};

char* arr_desc_test_option[] = {
	"test <n>: select test id",
	"size <n>: memory allocation size per a request",
	"iter <n>: number of times memory allocation requests are repeated",
	"nthreads <n>: number of threads to run specified tests",
	"time: display test execution time after the test is completed",
	"vsizes: variable memory allocation request sizes; 8B, 64B, 512B, 4KB and 2MB",
	"perthreadcpu: set different cpu affinities for each thread (applied only when nthreads > 1)",
	"exmem: set memtype to 'ExMem'(otherwise it is set to 'Normal')",
};

void print_test_desc(int num_tests) {
	printf("Test information:\n");
	for(int i = 0; i < num_tests; i++){
		printf("\t%2d: %s\n", i+1, arr_test[i].desc_test);
	}
}

void print_test_options(int num_options) {
	printf("Test options:\n");
	for(int i = 0; i < num_options; i++){
		printf("\t%s\n", arr_desc_test_option[i]);
	}
}

void assign_to_this_core(int core_id) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core_id, &mask);
	int ret = sched_setaffinity(0, sizeof(mask), &mask);
	assert_d_eq(ret, 0, "sched_setaffinity failure");
}

int main(int argc, char* argv[]){
	/* init variables */
	int test_idx = -1;
	int max_num_tests = sizeof(arr_test)/sizeof(test_t);
	long num_cores = 0;
	bool per_thread_cpu = false;
	unsigned int nthreads = NTHREADS_DEFAULT;
	void* status;

	pthread_t test_threads[MAX_NUM_THREADS] = {0,};
	test_param_t test_params[MAX_NUM_THREADS] = {0,};
	test_param_t test_param = {
		.nsizes = NSIZES_DEFAULT,
		.iter = ITER_DEFAULT,
		.type = SMDK_MEM_NORMAL,
		.is_time_needed = false
	};

	/* parse params */
	switch(argc) {
		case 1:
			printf("[PARAM ERROR] Choose test id from 1 to %u\n", max_num_tests);
			goto param_err;
		default:
			for(int i = 1; i < argc; i++) {
				if (!strcmp(argv[i], "size")) {
					size_arr[0] = (size_t)atoi(argv[++i]); //using size_arr[0] only
					test_param.nsizes = 1;
				} else if (!strcmp(argv[i], "iter")) {
					test_param.iter = (unsigned int)atoi(argv[++i]);
				} else if (!strcmp(argv[i], "nthreads")) {
					nthreads = (unsigned int)atoi(argv[++i]);
				} else if (!strcmp(argv[i], "time")) {
					test_param.is_time_needed = true;
				} else if (!strcmp(argv[i], "vsizes")) {
					test_param.nsizes = sizeof(size_arr) / sizeof(size_t);
				} else if (!strcmp(argv[i], "perthreadcpu")) {
					per_thread_cpu = true;
					num_cores = sysconf(_SC_NPROCESSORS_CONF);
				} else if (!strcmp(argv[i], "exmem")) {
					test_param.type = SMDK_MEM_EXMEM;
				} else if (!strcmp(argv[i], "test")) {
					test_idx = atoi(argv[++i]) - 1;
				} else {
					printf("[PARAM ERROR] argv[%d]: %s, Please check user input\n", i, argv[i]);
					goto param_err;
				}
			}
			break;
	}
	if (test_idx < 0) {
		printf("[PARAM ERROR] Test id is missing\n");
		goto param_err;
	}
	printf("[Test Parameters] size=");
	for (unsigned int i = 0 ; i < test_param.nsizes; i ++) {
		printf("%zu ", size_arr[i]);
	}
	printf(",iter=%u, nthreads=%u, mem type=%d\n", test_param.iter, nthreads, test_param.type);

	/* run tests */
	GET_TIME_START(&test_param);
	for(unsigned int j = 0; j < nthreads; j++) {
		memcpy(&test_params[j], &test_param, sizeof(test_param_t));
		if (per_thread_cpu) {
			assign_to_this_core(j % num_cores);
		}
		pthread_create(&test_threads[j], NULL, arr_test[test_idx].f_test, (void*)&(test_params[j]));
		//printf("create thread id: %ld\n", test_threads[j]);
		PRINT_TEST(test_idx+1, j, COLOR_RED, "Start");
	}
	for(unsigned int j = 0; j < nthreads; j++) {
		int ret = pthread_join(test_threads[j], (void**)&status);
		//printf("join thread id: %ld\n", test_threads[j]);
		assert_d_eq(ret, 0, "pthread_join failure");
		PRINT_TEST(test_idx+1, j, COLOR_GREEN, "End");
	}
	GET_TIME_END(&test_param);
	PRINT_ELAPSED_TIME(&test_param);
	goto ret;

param_err:
	print_test_desc(max_num_tests);
	print_test_options(sizeof(arr_desc_test_option)/sizeof(char*));

ret:
	return 0;
}
