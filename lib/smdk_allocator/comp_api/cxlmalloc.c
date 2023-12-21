#include "internal/cxlmalloc.h"
#include "internal/monitor.h"
#include "jemalloc/jemalloc.h"
#include <dlfcn.h>
#include <numa.h>

inline bool is_cxlmalloc_initialized(void) {
    return smdk_info.smdk_initialized;
}

inline bool is_use_exmem(void) {
    return opt_smdk.use_exmem;
}

int init_cxlmalloc(void) {
    if (likely(is_cxlmalloc_initialized())) {
        if (opt_smdk.adaptive_interleaving_policy ==
				policy_weighted_interleaving)
            init_monitor_thread();
		return SMDK_RET_SUCCESS;
    }

    smdk_init_helper("CXLMALLOC_CONF", false);
    int ret = init_smdk();
    if (ret == SMDK_RET_USE_EXMEM_FALSE) {
        fprintf(stderr, "[Warning] \"use_exmem:false\". Jemalloc would be used instead.\n");
        return ret;
    }

    if (init_dlsym() == SMDK_RET_INIT_DLSYM_FAIL) exit(1);

    init_monitor_thread();

    return ret;
}

inline int get_current_prio(void){
    int prio;
    pthread_rwlock_rdlock(&smdk_info.rwlock_current_prio);
    prio = smdk_info.current_prio;
    pthread_rwlock_unlock(&smdk_info.rwlock_current_prio);
    return prio;
}

inline mem_type_t get_cur_prioritized_memtype(void){
    return opt_smdk.prio[get_current_prio()];
}

static int do_maxmemory_policy(mem_type_t type_mem){
    switch(smdk_info.maxmemory_policy){
        case policy_oom:
            fprintf(stderr,"OOM - memory request exceeds limit\n");
            fprintf(stderr,"%s: terminated - maxmemory_policy=oom, current_prio=%d\n", __FUNCTION__,smdk_info.current_prio);
            exit(1);
            break;
        case policy_interleave:
            /* reset allocated size (pool 0/1) */
            pthread_rwlock_wrlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);
            smdk_info.alloc_mgmt[type_mem].size_alloc = 0;
            pthread_rwlock_unlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);

            pthread_rwlock_wrlock(&smdk_info.alloc_mgmt[!type_mem].rwlock_size_alloc);
            smdk_info.alloc_mgmt[!type_mem].size_alloc = 0;
            pthread_rwlock_unlock(&smdk_info.alloc_mgmt[!type_mem].rwlock_size_alloc);

            /* change priority 1-> 0 */
            pthread_rwlock_wrlock(&smdk_info.rwlock_current_prio);
            smdk_info.current_prio = 0;
            pthread_rwlock_unlock(&smdk_info.rwlock_current_prio);
            fprintf(stderr,"%s: CHANGE Priority 1->0\n",__FUNCTION__);

            break;
        case policy_remain:
            /* reset allocated size (current priority, 1) */
            pthread_rwlock_wrlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);
            smdk_info.alloc_mgmt[type_mem].size_alloc = 0;
            pthread_rwlock_unlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);

            fprintf(stderr,"%s:  REMAIN 1->1\n",__FUNCTION__);
            break;
        default:
            fprintf(stderr,"%s:  invalid policy %d\n",__FUNCTION__,smdk_info.maxmemory_policy);
            assert(false);
    }
    return 0;
}

inline void *malloc_internal(mem_type_t type, size_t size, bool zeroed, size_t alignment) {
    if (unlikely(!is_memtype_valid(type))) return NULL;

    if (unlikely(opt_smdk.use_adaptive_interleaving)) {
        int pool_id;
        void *ret = NULL;
        int socket = cpumap[malloc_getcpu()];
        while(1) {
            pool_id = get_best_pool_id(socket);
            if (pool_id == -1)
                goto default_fallback;

            ret = __s_malloc_internal(type, size, zeroed, alignment, pool_id);
            if (ret)
                break;
        }

        return ret;
    }

default_fallback:
    return s_malloc_internal(type, size, zeroed, alignment);
}

void *__mmap_internal_body(void *start, size_t len, int prot,
		int flags, int fd, off_t off, struct bitmask *nodemask)
{
    int ret;
    char *err_msg;
    void *addr = opt_syscall.mmap(start, len, prot, flags, fd, off);

    if (unlikely(addr == MAP_FAILED)) {
        err_msg = "mmap failed with return = MAP_FAILED";
        fprintf(stderr, "%s errno = %d: %s\n",
                err_msg, errno, strerror(errno));
        munmap(addr, len);
        return NULL;
    }

    ret = mbind(addr, len, MPOL_BIND, nodemask->maskp, nodemask->size + 1, 0);
    if (unlikely(ret)) {
        err_msg = "mbind(MPOL_BIND) failed with return";
        fprintf(stderr, "%s = %d, errno = %d: %s\n",
                err_msg, ret, errno, strerror(errno));
        return NULL;
    }

    ret = madvise(addr, len, MADV_TRY_POPULATE_WRITE);
    if (unlikely(ret)) {
        if (errno != ENOMEM) {
            err_msg = "madvise(MADV_TRY_POPULATE_WRITE) failed with return";
            fprintf(stderr, "%s = %d, errno = %d: %s\n",
                    err_msg, ret, errno, strerror(errno));
        }
        munmap(addr, len);
        return NULL;
    }

    ret = mbind(addr, len, MPOL_DEFAULT,
            numa_no_nodes_ptr->maskp, nodemask->size + 1, 0);
    if (unlikely(ret)) {
        err_msg = "mbind(MPOL_DEFAULT) failed with return";
        fprintf(stderr, "%s = %d, errno = %d: %s\n",
                err_msg, ret, errno, strerror(errno));
        munmap(addr, len);
        return NULL;
    }

    return addr;
}

void *__mmap_internal(void *start, size_t len, int prot,
		int flags, int fd, off_t off, int socket)
{
    void *addr = NULL;
    struct bitmask *nodemask;
    int prio = get_current_prio();
    info_node *info = fallback->list_node[socket];
    mem_type_t type = opt_smdk.prio[prio];

    for (int i=0;i<fallback->nr_node;i++) {
        if (info[i].type_mem != type)
            continue;
        int pool_id = info[i].pool_id;
        nodemask = g_arena_pool[pool_id].nodemask;
        addr = __mmap_internal_body(start, len, prot, flags,
            fd, off, nodemask);
        if (addr)
            break;
    }

    return addr;
}

inline void *mmap_internal(void *start, size_t len, int prot,
		int flags, int fd, off_t off, int socket)
{
    if (unlikely(opt_smdk.use_adaptive_interleaving)) {
        void *addr;
        struct bitmask *nodemask;
        while(1) {
            int pool_id = get_best_pool_id(socket);
            if (pool_id == -1)
                goto default_fallback;
            nodemask = g_arena_pool[pool_id].nodemask;
            addr = __mmap_internal_body(start, len, prot, flags,
                    fd, off, nodemask);
            if (addr)
                break;
        }
        return addr;
    }

default_fallback:
    return __mmap_internal(start, len, prot, flags, fd, off, socket);
}

inline int update_arena_pool(int prio, size_t allocated){
    size_t size_alloc, size_limit;
    mem_type_t type_mem = opt_smdk.prio[prio];

    pthread_rwlock_wrlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);
    smdk_info.alloc_mgmt[type_mem].size_alloc += allocated; /* bytes */
    pthread_rwlock_unlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);

    pthread_rwlock_rdlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);
    size_alloc = smdk_info.alloc_mgmt[type_mem].size_alloc;
    size_limit = (type_mem == mem_type_normal) ? opt_smdk.normal_pool_size : opt_smdk.exmem_pool_size;

    if (opt_smdk.use_adaptive_interleaving) {
        pthread_rwlock_unlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);
    } else if (size_alloc > size_limit * MB) { /* zone_limit: MB */
        pthread_rwlock_unlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);
        if(get_current_prio() != prio){
            return 0;
        } else if (smdk_info.current_prio == 0) {
            pthread_rwlock_wrlock(&smdk_info.rwlock_current_prio);
            smdk_info.current_prio = 1;
            pthread_rwlock_unlock(&smdk_info.rwlock_current_prio);
            fprintf(stderr,"%s: CHANGE Priority 0->1\n", "update_arena_pool");
        } else {
            do_maxmemory_policy(type_mem);
        }
    } else {
        pthread_rwlock_unlock(&smdk_info.alloc_mgmt[type_mem].rwlock_size_alloc);
    }
    return 0;
}

tr_syscall_config opt_syscall = {
    .mmap = NULL,
    .mmap64 = NULL,
    .malloc = NULL,
    .calloc = NULL,
    .realloc = NULL,
    .posix_memalign = NULL,
    .aligned_alloc = NULL,
    .free = NULL,
    .malloc_usable_size = NULL,
    .is_initialized = false,
};

inline bool is_tr_syscall_initialized(void){
    return opt_syscall.is_initialized;
}

inline int init_dlsym(void){
    if (is_tr_syscall_initialized()) {
        return SMDK_RET_SUCCESS;
    }

    opt_syscall.mmap = (mmap_ptr_t)dlsym(RTLD_NEXT, "mmap");
    if (opt_syscall.mmap == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"mmap\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

    opt_syscall.mmap64 = (mmap_ptr_t)dlsym(RTLD_NEXT, "mmap64");
    if (opt_syscall.mmap64 == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"mmap64\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

    if (opt_smdk.adaptive_interleaving_policy != policy_weighted_interleaving)
        goto finish;

    opt_syscall.malloc = (malloc_ptr_t)dlsym(RTLD_NEXT, "malloc");
    if (opt_syscall.malloc == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"malloc\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

    opt_syscall.calloc = (calloc_ptr_t)dlsym(RTLD_NEXT, "calloc");
    if (opt_syscall.calloc == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"calloc\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

    opt_syscall.realloc = (realloc_ptr_t)dlsym(RTLD_NEXT, "realloc");
    if (opt_syscall.realloc == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"realloc\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

    opt_syscall.posix_memalign = (posix_memalign_ptr_t)dlsym(RTLD_NEXT, "posix_memalign");
    if (opt_syscall.posix_memalign == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"posix_memalign\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

    opt_syscall.aligned_alloc = (aligned_alloc_ptr_t)dlsym(RTLD_NEXT, "aligned_alloc");
    if (opt_syscall.aligned_alloc == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"aligned_alloc\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

    opt_syscall.free = (free_ptr_t)dlsym(RTLD_NEXT, "free");
    if (opt_syscall.free == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"free\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

    opt_syscall.malloc_usable_size = (malloc_usable_size_ptr_t)dlsym(RTLD_NEXT, "malloc_usable_size");
    if (opt_syscall.malloc_usable_size == NULL) {
        fprintf(stderr, "Error in dlsym(RTLD_NEXT,\"malloc_usable_size\")\n");
        return SMDK_RET_INIT_DLSYM_FAIL;
    }

finish:
    opt_syscall.is_initialized = true;
    return SMDK_RET_SUCCESS;
}

inline void change_mmap_ptr_mmap64(void){
    opt_syscall.mmap = opt_syscall.mmap64;
}

inline size_t malloc_usable_size_internal (mem_type_t type, void *ptr) { /* added for redis */
    if (unlikely(!is_memtype_valid(type))) return 0;
    return je_malloc_usable_size(ptr);
}

void terminate_cxlmalloc(void){
    if (smdk_info.smdk_initialized == false)
        return;
    terminate_smdk();
}

inline bool is_weighted_interleaving()
{
    if (weighted_interleaving)
        return true;

    smdk_init_helper("CXLMALLOC_CONF", false);
    if (opt_smdk.adaptive_interleaving_policy == policy_weighted_interleaving) {
        if (smdk_info.smdk_initialized)
            return true;

        if (unlikely(init_dlsym() != SMDK_RET_SUCCESS))
            exit(1);

        smdk_info.smdk_initialized = true;

        return true;
    }

    return false;
}
