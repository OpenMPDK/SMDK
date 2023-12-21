#ifndef INIT_H_
#define INIT_H_

#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/atomic.h"
#include "jemalloc/internal/ctl.h"
#include "jemalloc/internal/extent_dss.h"
#include "jemalloc/internal/extent_mmap.h"
#include "jemalloc/internal/hook.h"
#include "jemalloc/internal/jemalloc_internal_types.h"
#include "jemalloc/internal/log.h"
#include "jemalloc/internal/malloc_io.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/rtree.h"
#include "jemalloc/internal/safety_check.h"
#include "jemalloc/internal/sc.h"
#include "jemalloc/internal/spin.h"
#include "jemalloc/internal/sz.h"
#include "jemalloc/internal/ticker.h"
#include "jemalloc/internal/util.h"

#include <assert.h>
#include <limits.h>

/* config smdk */
#define UNLIMITED (-1)
#define MEMPOOL_UNLIMITED SIZE_MAX
#define MAX_MEMPOOL_LIMIT_MB    (1024 * 1024)    /* temporal limit, 1TB */
#define NR_ARENA_MAX 128
#define ARENA_AUTOSCALE_FACTOR 0
#define ARENA_SCALE_FACTOR 2

/* utility constants */
#ifndef GB
#define GB (1<<30)
#endif

#ifndef MB
#define MB (1<<20)
#endif

#ifndef KB
#define KB (1024)
#endif

#define SMDK_EXPORT __attribute__((visibility("default")))
#define SMDK_CONSTRUCTOR(n) __attribute__((constructor ((n))))
#define SMDK_DESTRUCTOR(n) __attribute__((destructor ((n))))
#define SMALLOC_CONSTRUCTOR_PRIORITY 102 /* ~100: reserved for system, 101: reserved for cxlmalloc */
#define SMALLOC_DESTRUCTOR_PRIORITY 102
#define SMDK_INLINE static inline

#define NAME_NORMAL "Normal"
#define NAME_EXMEM "ExMem"
#define NAME_ZONE_NORMAL "Normal"
#define NAME_ZONE_MOVABLE "Movable"
#define INVALID_MEM_SIZE (size_t)0

#define MAX_CHAR_LEN 200 /* metadata API */
#define MAX_NUMA_DISTANCE (255)

typedef enum {
    mem_type_normal=0,     /* point Normal type of memory */
    mem_type_exmem,        /* point ExMem type of memory */
    mem_type_invalid       /* point invalid type */
} mem_type_t;

typedef struct { //metaAPI
    size_t total;
    size_t available;
} mem_stats_per_node;

typedef struct { //metaAPI
    size_t total;
    size_t used;
    size_t available;
} mem_stats_t;

typedef struct smdk_config{
    bool use_exmem;
    mem_type_t prio[2];
    size_t exmem_pool_size;        /* byte in size */
    size_t normal_pool_size;    /* byte in size */
    bool use_auto_arena_scaling;
    bool use_adaptive_interleaving;
    int adaptive_interleaving_policy;
    int maxmemory_policy;            /* what will you do on 2nd pool goes to maxmemory */
    char exmem_partition_range[MAX_CHAR_LEN];
    char interleave_node[MAX_CHAR_LEN];
    bool is_parsed;
}smdk_config;
extern smdk_config opt_smdk;

typedef struct arena_pool{
    unsigned arena_id[NR_ARENA_MAX];    /* normal/exmem arena id */
    int nr_arena;                        /* the number of normal/exmem arena in arena pool */
    int arena_index;                    /* arena index, used only when use_auto_arena_scaling=false to traverse avaiable arena index(round-robin)*/
    pthread_rwlock_t rwlock_arena_index;
    mem_type_t type_mem;
    struct bitmask *nodemask;
}arena_pool;
extern arena_pool* g_arena_pool;
typedef unsigned (*get_target_arena_t)(int pool_id);

typedef struct mem_allocated{
    size_t size_alloc;
    pthread_rwlock_t rwlock_size_alloc;
}alloc_status;

typedef struct smdk_param{
    int current_prio;
    int nr_pool; /* the number of arena pools (normal + exmem) */
    int nr_pool_normal; /* the number of normal pools */
    int nr_pool_exmem;  /* the number of exmem pools */
    int nr_arena_normal; /* the number of normal arenas for all pools */
    int nr_arena_exmem;  /* the number of exmem arenas for all pools */
    int maxmemory_policy;
    bool smdk_initialized;
    get_target_arena_t get_target_arena;
    pthread_rwlock_t rwlock_current_prio;
    alloc_status alloc_mgmt[2]; /* normal, exmem */
    mem_stats_t stats_per_type[2]; //metaAPI
    mem_stats_per_node *stats_per_node; //metaAPI
    struct bitmask *exmem_range_bitmask;
}smdk_param;
extern smdk_param smdk_info;

typedef struct info_node{
    int pool_id;
    int node_id;
    int val; /* numa distance */
    mem_type_t type_mem;
    struct bitmask *nodemask;
}info_node;

typedef struct smdk_fallback{
    int nr_node; /* the number of nodes(=pools) */
    int nr_node_normal; /* the number of normal nodes */
    int nr_node_exmem; /* the number of exmem nodes */
    info_node **list_node;
}smdk_fallback;
extern smdk_fallback* fallback;
extern int* cpumap;

extern int init_smdk();
void init_fallback_order();
extern void terminate_smdk();

#endif /* INIT_H_ */
