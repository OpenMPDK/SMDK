#ifndef CXLMALLOC_H_
#define CXLMALLOC_H_

#include "core/include/internal/init.h"
#include "core/include/internal/config.h"
#include "core/include/internal/alloc.h"

mem_type_t get_cur_prioritized_memtype(void);
bool is_tr_syscall_initialized(void);
void change_mmap_ptr_mmap64(void);
int update_arena_pool(int prio, size_t allocated);
int init_dlsym(void);
int init_cxlmalloc(void);
void terminate_cxlmalloc(void);
size_t malloc_usable_size_internal (mem_type_t type, void *ptr);
bool is_use_exmem(void);
bool is_cxlmalloc_initialized(void);
int get_current_prio(void);
extern bool weighted_interleaving;
void *malloc_internal(mem_type_t type, size_t size, bool zeroed, size_t alignment);
void *mmap_internal(void *start, size_t len, int prot, int flags, int fd, off_t off, int socket);
bool is_weighted_interleaving(void);

#define CXLMALLOC_PRECONDITION(n) do {				    \
        if (unlikely(!is_use_exmem())) {			    \
            return je_##n;					    \
        } else if (unlikely(is_cxlmalloc_initialized() == false)) { \
            return je_##n;					    \
        }							    \
    }while (0)
#define MAP_JEMALLOC_INTERNAL_MMAP 0x800000
#define RET_USE_EXMEM_FALSE (1)
#define CXLMALLOC_CONSTRUCTOR_PRIORITY (SMALLOC_CONSTRUCTOR_PRIORITY-1)
#define CXLMALLOC_DESTRUCTOR_PRIORITY (SMALLOC_DESTRUCTOR_PRIORITY-1)
typedef void *(*mmap_ptr_t)(void *, size_t, int, int, int, off_t);
typedef void *(*malloc_ptr_t)(size_t);
typedef void *(*calloc_ptr_t)(size_t, size_t);
typedef void *(*realloc_ptr_t)(void *, size_t);
typedef int (*posix_memalign_ptr_t)(void **, size_t, size_t);
typedef void *(*aligned_alloc_ptr_t)(size_t, size_t);
typedef void (*free_ptr_t)(void *);
typedef size_t (*malloc_usable_size_ptr_t)(void *);

typedef struct {
    mmap_ptr_t mmap;
    mmap_ptr_t mmap64;
    malloc_ptr_t malloc;
    calloc_ptr_t calloc;
    realloc_ptr_t realloc;
    posix_memalign_ptr_t posix_memalign;
    aligned_alloc_ptr_t aligned_alloc;
    malloc_usable_size_ptr_t malloc_usable_size;
    free_ptr_t free;
    bool is_initialized;
    unsigned char calloc_buf[4096];
} tr_syscall_config;
extern tr_syscall_config opt_syscall;

#endif /* CXLMALLOC_H_ */
