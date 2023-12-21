/*
   Copyright, Samsung Corporation

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in
 the documentation and/or other materials provided with the
 distribution.

 * Neither the name of the copyright holder nor the names of its
 contributors may be used to endorse or promote products derived
 from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "internal/init.h"
#include "internal/alloc.h"
#include "internal/config.h"
#include "jemalloc/jemalloc.h"
#include <sys/sysinfo.h>

SMDK_INLINE
unsigned get_arena_idx(int pool_id) {
    return smdk_info.get_target_arena(pool_id);
}

SMDK_INLINE
int get_mallocx_tcache_idx(size_t size, int pool_id) {
    unsigned *list_tcache = get_tsd_list_tcache();
    if (unlikely(list_tcache == NULL)) {
        DP("list_tcache NULL. alloc list_tcache...\n");
        list_tcache = je_calloc(smdk_info.nr_pool, sizeof(unsigned));
        if (list_tcache == NULL) {
            DP("cant alloc list_tcache. return MALLOCX_TCACHE_NONE..\n");
            return MALLOCX_TCACHE_NONE;
        }
        set_tsd_list_tcache(list_tcache);
    }

    if (unlikely(list_tcache[pool_id] == 0)) {
        DP("list_tcache[%d] NULL. alloc list_tcache[%d]...\n", pool_id, pool_id);
        size_t sz = sizeof(unsigned);
        if (je_mallctl("tcache.create", (void *)&list_tcache[pool_id], &sz, NULL, 0)) {
            fprintf(stderr, "list_tcache[%d] tcache.create failrue\n", pool_id);
            return MALLOCX_TCACHE_NONE;
        }
    }
    return MALLOCX_TCACHE(list_tcache[pool_id]);
}

SMDK_INLINE
int get_pool_id_from_node_id(mem_type_t type, int node_id){
    for(int i=0;i<fallback->nr_node;i++){ /* nr_node = nr_pool */
        info_node *info = &(fallback->list_node)[0][i];
        if (type != info->type_mem)
            continue;
        if (info->node_id == node_id)
            return info->pool_id;
    }
    return -1;
}

inline bool is_memtype_valid(mem_type_t memtype) {
    return ((memtype == mem_type_normal) || (memtype == mem_type_exmem));
}

inline mem_type_t get_memtype_from_pid(int pid) {
    if (unlikely(pid == -1)) {
        return mem_type_invalid;
    }
    return g_arena_pool[pid].type_mem;
}

inline void *__s_malloc_internal(mem_type_t type, size_t size,
        bool zeroed, size_t alignment, int pool_id)
{
    int flags;

    flags = MALLOCX_ARENA(get_arena_idx(pool_id)) | get_mallocx_tcache_idx(size, pool_id);
    if (zeroed) {
        flags |= MALLOCX_ZERO;
    }
    if (alignment) {
        flags |= MALLOCX_ALIGN(alignment);
    }

    return je_mallocx(size, flags);
}

inline void *s_malloc_internal(mem_type_t type, size_t size, bool zeroed, size_t alignment) {
    if (unlikely(!is_memtype_valid(type))) return NULL;

    int pool_id;
    void *ret = NULL;
    int socket = cpumap[malloc_getcpu()];
    info_node *info = fallback->list_node[socket];

    for (int i=0;i<fallback->nr_node;i++) {
        if (info[i].type_mem != type)
            continue;

        pool_id = info[i].pool_id;
        ret = __s_malloc_internal(type, size, zeroed, alignment, pool_id);
        if (ret)
            break;
    }

    return ret;
}

inline void *s_malloc_internal_node(mem_type_t type, size_t size, int node_id) {
	void *ret = NULL;
	int pool_id = get_pool_id_from_node_id(type, node_id);
	if (pool_id >= 0)
		ret = __s_malloc_internal(type, size, false, 0, pool_id);
	return ret;
}

inline void *s_realloc_internal(mem_type_t type, void *ptr, size_t size) {
    if (unlikely(!is_memtype_valid(type))) return NULL;

    if (unlikely(size == 0 && ptr != NULL)) { //call free
        mem_type_t old_type = get_memtype_from_pid(je_arenaidx_pid(ptr));
        if (likely(old_type != mem_type_invalid)) {
            s_free_internal(ptr);
        }
        return NULL;
    } else {
        if (unlikely(ptr == NULL)) {
            return s_malloc_internal(type, size, false, 0);
        } else {
            mem_type_t old_type = get_memtype_from_pid(je_arenaidx_pid(ptr));
            if (unlikely(old_type == mem_type_invalid)) { //invalid ptr
                return NULL;
            } else {
                void* ret = s_malloc_internal(type, size, false, 0);
                if (likely(ret)) {
                    size_t old_size = je_usize_find(ptr);
                    size_t copy_size = (size < old_size) ? size: old_size;
                    memcpy(ret, ptr, copy_size);
                }
                s_free_internal(ptr);
                return ret;
            }
        }
    }
}

inline int s_posix_memalign_internal(mem_type_t type, void **memptr, size_t alignment, size_t size) {
    if (unlikely(!is_memtype_valid(type))) return EINVAL;

    *memptr = s_malloc_internal(type, size, false, alignment);
    if (*memptr == NULL) {
        return ENOMEM;
    }
    return 0;
}

inline void *s_aligned_alloc_internal(mem_type_t type, size_t alignment, size_t size) {
    if (unlikely(!is_memtype_valid(type))) {
        set_errno(EINVAL);
        return NULL;
    }
    return s_malloc_internal(type, size, false, alignment);
}

inline void s_free_internal(void *ptr) {
    if (unlikely(ptr == NULL)) return;

    je_dallocx(ptr, get_mallocx_tcache_idx(0, je_arenaidx_pid(ptr)));
}

inline void s_free_internal_type(mem_type_t type, void *ptr) {
    if (unlikely(ptr == NULL)) return;
    if (unlikely(!is_memtype_valid(type))) return;

    je_dallocx(ptr, get_mallocx_tcache_idx(0, je_arenaidx_pid(ptr)));
}
