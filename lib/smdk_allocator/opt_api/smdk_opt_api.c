#include "smdk_opt_api.h"
#include "internal/opt_api.h"
#include "internal/config.h"

SMDK_EXPORT
void * s_malloc(smdk_memtype_t type, size_t size) {
	return s_malloc_internal(type, size, false);
}

SMDK_EXPORT
void* s_calloc(smdk_memtype_t type, size_t num, size_t size) {
    return s_malloc_internal(type, num * size, true);
}

SMDK_EXPORT
void* s_realloc(smdk_memtype_t type, void *ptr, size_t size) {
	return s_realloc_internal(type, ptr, size);
}

SMDK_EXPORT
int s_posix_memalign(smdk_memtype_t type, void **memptr, size_t alignment, size_t size) {
	return s_posix_memalign_internal(type, memptr, alignment, size);
}

SMDK_EXPORT
void s_free(void *ptr) {
	return s_free_internal(ptr);
}

SMDK_EXPORT
void s_free_type(smdk_memtype_t type, void *ptr) {
	return s_free_internal_type(type, ptr);
}

SMDK_EXPORT
size_t s_get_memsize_total(smdk_memtype_t type) { //metaAPI
	if (unlikely(!is_memtype_valid(type))) return INVALID_MEM_SIZE;
    return get_mem_stats_total(type);
}

SMDK_EXPORT
size_t s_get_memsize_used(smdk_memtype_t type) { //metaAPI
	if (unlikely(!is_memtype_valid(type))) return INVALID_MEM_SIZE;
    return get_mem_stats_used(type);
}

SMDK_EXPORT
size_t s_get_memsize_available(smdk_memtype_t type) { //metaAPI
	if (unlikely(!is_memtype_valid(type))) return INVALID_MEM_SIZE;
    return get_mem_stats_available(type);
}

SMDK_EXPORT
void s_stats_print(void) {
    mem_stats_print();
}

SMDK_EXPORT
void s_stats_node_print(void) {
    stats_per_node_print();
}

SMDK_CONSTRUCTOR(SMALLOC_CONSTRUCTOR_PRIORITY)
static void
smalloc_constructor(void) {
    init_smalloc();
    show_smdk_info(true);
}
