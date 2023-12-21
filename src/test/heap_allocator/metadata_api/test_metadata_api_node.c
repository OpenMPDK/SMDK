#include <string.h>
#include <unistd.h>
#include <numa.h>
#include "test.h"
#include "smdk_opt_api.h"
/*
 * Repeat the allocation request until the total requested memory reaches a threshold.
 * After that, compare the application request amount and the metadata information from 's_get_memsize_used'.
 * cf. s_get_memsize_total and s_get_memsize_available provide information based on total system.
 */

#define KiB (1<<10)
#define GiB (1<<30)
#define TCACHE_SMALL_MAX (14*KiB)
#define TCACHE_LARGE_MAX (32*KiB)
#define MAX_NFILLS (200)
#define TCACHE_NSLOTS_SMALL_MAX (200) //Absolute maximum number of cache slots for each bin(<=32KiB)
#define TCACHE_NSLOTS_LARGE (20)

void print_node_stat(){
	printf("\n  testing size returning function \n");
	for(int i=0; i<=numa_max_node(); i++){
		printf("node %d, zone normal, total     : %zu\n"
		       ,i,s_get_memsize_node_total(SMDK_MEM_NORMAL,i));
		printf("node %d, zone exmem , total     : %zu\n"
		       ,i,s_get_memsize_node_total(SMDK_MEM_EXMEM,i));
		printf("node %d, zone normal, available : %zu\n"
		       ,i,s_get_memsize_node_available(SMDK_MEM_NORMAL,i));
		printf("node %d, zone exmem , available : %zu\n"
		       ,i,s_get_memsize_node_available(SMDK_MEM_EXMEM,i));
	}
}

void test(size_t size, size_t total, smdk_memtype_t type){
	printf("[%s] size=%zu, total=%zuGiB, type=%d\n", __FUNCTION__, size, total/GiB, type);

	unsigned iter = (unsigned)(total / size);
	char** buf = malloc(sizeof(char*)*iter);
	assert_ptr_not_null(buf, "malloc allocation failure");

	s_stats_node_print('G');
	print_node_stat();

	for(unsigned i = 0; i < iter ; i++){
		buf[i] = (char*)s_malloc(type, size);
		memset(buf[i], '0', size);
	}

	print_node_stat();

	for(unsigned i = 0; i < iter ; i++){
		s_free_type(type, buf[i]);
	}

	print_node_stat();
	return;
}

int main(int argc, char* argv[]) {
	smdk_memtype_t type = SMDK_MEM_NORMAL;
	size_t size_shift = 30;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "size"))
			size_shift = (size_t)atoi(argv[++i]);
	}

	size_t size_base = 1 << size_shift;

	s_stats_print('G');

	/* Notice: request size should be same with sc_size in jemalloc */
	/* case 1: tcache_small(<=14KiB) */
	test(4*KiB, 1*size_base, type);

	/* case 2: tcache_large(<=32KiB) */
	test(16*KiB, 4*size_base, type);

	/* case 3: huge(>32KiB) */
	test(64*KiB, 8*size_base, type);

	s_stats_print('G');
	return 0;
}
