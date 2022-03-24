#include <string.h>
#include <unistd.h>
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


void test(size_t size, size_t total, smdk_memtype_t type){
	printf("[%s] size=%zu, total=%zuGiB, type=%d\n", __FUNCTION__, size, total/GiB, type);

	size_t mem_requested = total;
	unsigned iter = (unsigned)(total / size);
	char** buf = malloc(sizeof(char*)*iter);
	assert_ptr_not_null(buf, "malloc allocation failure");

	size_t mem_used, mem_used_after = 0;
	size_t mem_used_before = s_get_memsize_used(type);
	size_t mem_available_after, mem_available_before = s_get_memsize_available(type);
	printf("\tmem_used (before malloc) = %zu\n", mem_used_before);
	printf("\tmem_requested = %zu\n", mem_requested);
	printf("\tmem_available (before malloc) = %zu\n\n", mem_available_before);

	/*
	 * Note that in the case of malloc which size is under tcache_small and tcache_large,
	 * as many objects as specified by 'ncached_max' of each tcache_bin are allocated to the tcache.
	 * At this time, the 'allocated' of the arena is increased by size_class * nfils. 
	 * Therefore, mem_used can be higher than mem_requested.
	 */
	for(unsigned i = 0; i < iter ; i++){
		buf[i] = (char*)s_malloc(type, size);
		memset(buf[i], '0', size);
	}
	mem_used_after = s_get_memsize_used(type);
	mem_used = mem_used_after - mem_used_before;
	mem_available_after = s_get_memsize_available(type);
	printf("\tmem_used (after malloc) = %zu\n", mem_used_after);
	//printf("\tmem_used for this test = %zu\n", mem_used);
	printf("\tmem_available (after malloc) = %zu\n\n", mem_available_after);

	if (size <= TCACHE_SMALL_MAX) {
		assert_zu_le(mem_used, mem_requested + (size * TCACHE_NSLOTS_SMALL_MAX),\
				"s_get_memsize_used reported wrong value");
	} else if (size <= TCACHE_LARGE_MAX) {
		assert_zu_le(mem_used, mem_requested + (size * TCACHE_NSLOTS_LARGE),\
				"s_get_memsize_used reported wrong value");
	} else {
		assert_zu_eq(mem_used, mem_requested,\
				"s_get_memsize_used reported wrong value");
	}

	/*
	 * Note that 'free' does not immediately reduce the value 'allocated' of the arenas,
	 * since the freed objects are re-allocated to tcache for reusing.
	 * (tcache_small and tcache_large obj)
	 */
	for(unsigned i = 0; i < iter ; i++){
		s_free_type(type, buf[i]);
	}
	mem_used_after = s_get_memsize_used(type);
	mem_used = mem_used_after - mem_used_before;
	mem_available_after = s_get_memsize_available(type);
	printf("\tmem_used (after free) = %zu\n", mem_used);
	printf("\tmem_available (after free) = %zu\n\n", mem_available_after);

	if (size <= TCACHE_SMALL_MAX) {
		assert_zu_le(mem_used, size * TCACHE_NSLOTS_SMALL_MAX,\
				"s_get_memsize_used reported wrong value");
	} else if (size <= TCACHE_LARGE_MAX) {
		assert_zu_le(mem_used, size * TCACHE_NSLOTS_LARGE,\
				"s_get_memsize_used reported wrong value");
	} else { 
		assert_zu_eq(mem_used, 0,\
				"s_get_memsize_used reported wrong value");
	}

	return;
}

int main(void){
	smdk_memtype_t type = SMDK_MEM_NORMAL;
	size_t size_gb = 1<<30;

	s_stats_print();
	s_stats_node_print();

	/* Notice: request size should be same with sc_size in jemalloc */
	/* case 1: tcache_small(<=14KiB) */
	test(4*KiB, 1*size_gb, type);

	/* case 2: tcache_large(<=32KiB) */
	test(16*KiB, 4*size_gb, type);
	s_stats_node_print();

	/* case 3: huge(>32KiB) */
	test(64*KiB, 8*size_gb, type);

	s_stats_print();
	s_stats_node_print();
	return 0;
}
