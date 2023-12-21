#include "core/include/internal/alloc.h"
#include "internal/cxlmalloc.h"
#include "jemalloc/jemalloc.h"

SMDK_EXPORT
void *malloc(size_t size) {
    if (is_weighted_interleaving()) {
        return opt_syscall.malloc(size);
    }

    CXLMALLOC_PRECONDITION(malloc(size));
    mem_type_t type = get_cur_prioritized_memtype();
    return malloc_internal(type, size, false, 0);
}

SMDK_EXPORT
void *calloc(size_t num, size_t size) {
    if (unlikely(!is_tr_syscall_initialized())) {
        return (void *)opt_syscall.calloc_buf;
    }
    if (is_weighted_interleaving()) {
        return opt_syscall.calloc(num, size);
    }

    CXLMALLOC_PRECONDITION(calloc(num, size));
    mem_type_t type = get_cur_prioritized_memtype();
    return malloc_internal(type, num * size, true, 0);
}

SMDK_EXPORT
void *realloc(void *ptr, size_t size) {
    if (is_weighted_interleaving()) {
        return opt_syscall.realloc(ptr, size);
    }

    CXLMALLOC_PRECONDITION(realloc(ptr, size));
    mem_type_t type = get_cur_prioritized_memtype();
    return s_realloc_internal(type, ptr, size);
}

SMDK_EXPORT
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (is_weighted_interleaving()) {
        return opt_syscall.posix_memalign(memptr, alignment, size);
    }

    CXLMALLOC_PRECONDITION(posix_memalign(memptr, alignment, size));
    mem_type_t type = get_cur_prioritized_memtype();
    return s_posix_memalign_internal(type, memptr, alignment, size);
}

SMDK_EXPORT
void *aligned_alloc(size_t alignment, size_t size) {
    if (is_weighted_interleaving()) {
        return opt_syscall.aligned_alloc(alignment, size);
    }

    CXLMALLOC_PRECONDITION(aligned_alloc(alignment, size));
    mem_type_t type = get_cur_prioritized_memtype();
    return s_aligned_alloc_internal(type, alignment, size);
}

SMDK_EXPORT
void free(void *ptr) {
    if (unlikely(ptr == (void *)opt_syscall.calloc_buf)) {
        return;
    }
    if (is_weighted_interleaving()) {
        return opt_syscall.free(ptr);
    }

    CXLMALLOC_PRECONDITION(free(ptr));
    return s_free_internal(ptr);
}

SMDK_EXPORT
void *mmap(void *start, size_t len, int prot, int flags, int fd, off_t off) {
    void *addr = NULL;
    if (unlikely(init_dlsym() != SMDK_RET_SUCCESS)) {
        fprintf(stderr, "init_dlsym() failed\n");
        return addr;
    }

    if (likely(smdk_info.smdk_initialized)) {
        int prio = get_current_prio();
        if (likely(flags & MAP_JEMALLOC_INTERNAL_MMAP)) {
            addr = opt_syscall.mmap(start, len, prot, flags, fd, off);
        } else {
            if (unlikely(weighted_interleaving || !(prot & PROT_WRITE) ||
                        fd != -1)) {
                addr = opt_syscall.mmap(start, len, prot, flags, fd, off);
            } else {
                int socket = cpumap[malloc_getcpu()];
                addr = mmap_internal(start, len, prot, flags & ~(MAP_POPULATE), fd, off, socket);
                if (!addr) {
                    addr = MAP_FAILED;
                }
            }
        }
        if (likely(addr !=MAP_FAILED && addr)){
            update_arena_pool(prio, len);
        }
    } else {
        addr = opt_syscall.mmap(start, len, prot, flags, fd, off);
    }

    return addr;
}

SMDK_EXPORT
void *mmap64(void *start, size_t len, int prot, int flags, int fd, off_t off) {
    change_mmap_ptr_mmap64();
    return mmap(start, len, prot, flags, fd, off);
}

SMDK_EXPORT
size_t malloc_usable_size (void *ptr) { /* added for redis */
    mem_type_t type = get_memtype_from_pid(je_arenaidx_pid(ptr));
    return malloc_usable_size_internal(type, ptr);
}

SMDK_CONSTRUCTOR(CXLMALLOC_CONSTRUCTOR_PRIORITY)
static void
cxlmalloc_constructor(void) {
    init_cxlmalloc();
    show_smdk_info(false);
}

SMDK_DESTRUCTOR(CXLMALLOC_DESTRUCTOR_PRIORITY)
static void
cxlmalloc_destructor(void) {
    terminate_cxlmalloc();
}
