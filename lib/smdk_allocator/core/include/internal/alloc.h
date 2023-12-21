#ifndef ALLOC_H_
#define ALLOC_H_

#include <numaif.h>
#include "internal/init.h"

#ifndef MADV_TRY_POPULATE_WRITE
#define MADV_TRY_POPULATE_WRITE (26)
#endif

extern void *s_malloc_internal(mem_type_t type, size_t size, bool zeroed, size_t alignment);
extern void *s_malloc_internal_node(mem_type_t type, size_t size, int node_id);
extern void *s_realloc_internal(mem_type_t type, void *ptr, size_t size);
extern int s_posix_memalign_internal(mem_type_t type, void **memptr, size_t alignment, size_t size);
extern void *s_aligned_alloc_internal(mem_type_t type, size_t alignment, size_t size);
extern void s_free_internal_type(mem_type_t type, void *ptr);
extern void s_free_internal(void *ptr);
extern void *__s_malloc_internal(mem_type_t type, size_t size, bool zeroed, size_t alignment, int pool_id);
extern bool is_memtype_valid(mem_type_t memtype);
extern mem_type_t get_memtype_from_pid(int pid);
extern int get_best_pool_id(int socket_id);

#endif /* ALLOC_H_ */
