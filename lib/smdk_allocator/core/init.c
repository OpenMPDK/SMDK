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
#include <numa.h>

smdk_config opt_smdk = {
    .use_exmem = true,
    .prio[0] = mem_type_normal, /* prio high */
    .prio[1] = mem_type_exmem, /* prio low */
    .exmem_pool_size = MAX_MEMPOOL_LIMIT_MB, /* MB in size */
    .normal_pool_size = MAX_MEMPOOL_LIMIT_MB, /* MB in size */
    .use_auto_arena_scaling = true, /* when enabled, create cpu*2 arenas */
    .use_adaptive_interleaving = false,
    .adaptive_interleaving_policy = 0, /* adaptive_interleaving_policy : bw_saturation */
    .maxmemory_policy = 0, /* maxmemory_policy : oom, interleave, remain */
    .exmem_partition_range = {0, },
    .is_parsed = false,
};

smdk_param smdk_info = {
    .current_prio = 0,
    .nr_pool = 0,
    .nr_pool_normal = 0,
    .nr_pool_exmem = 0,
    .nr_arena_normal = 0,
    .nr_arena_exmem = 0,
    .smdk_initialized = false,
    .get_target_arena = NULL,
    .exmem_range_bitmask = NULL,
};

arena_pool *g_arena_pool;
smdk_fallback* fallback;
int *cpumap;

SMDK_INLINE unsigned get_auto_scale_target_arena(int pool_id){
    malloc_cpuid_t cpuid = malloc_getcpu();
    assert(cpuid >= 0);
    arena_pool* pool = &g_arena_pool[pool_id];

    return pool->arena_id[cpuid%pool->nr_arena];
}

SMDK_INLINE unsigned get_normal_target_arena(int pool_id){
    return g_arena_pool[pool_id].arena_id[0];
}

static int scale_arena_pool(){
    int nrcpu = sysconf(_SC_NPROCESSORS_ONLN);
    assert(nrcpu >0);

    int narenas = 1;
    if(opt_smdk.use_auto_arena_scaling){
        /* for SMP systems, create 2 normal/exmem arena per cpu by default */
        narenas = nrcpu << ARENA_AUTOSCALE_FACTOR;
    }
    smdk_info.nr_arena_normal = MIN(narenas, NR_ARENA_MAX);
    smdk_info.nr_arena_exmem = MIN(narenas, NR_ARENA_MAX);
    return narenas;
}

static int init_smdk_rwlock(pthread_rwlock_t* lock){
    if(!lock)
        return -1;

    pthread_rwlockattr_t attr;

    if (pthread_rwlockattr_init(&attr) != 0) {
        return 1;
    }
    pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE);
    if (pthread_rwlock_init(lock, &attr) != 0) {
        pthread_rwlockattr_destroy(&attr);
        return 1;
    }
    pthread_rwlockattr_destroy(&attr);
    return 0;
}

static mem_type_t get_type_mem_from_pool_id(int pool_id){
    for(int i=0;i<fallback->nr_node;i++){ /* nr_node = nr_pool */
        info_node *info = &(fallback->list_node)[0][i];
        if (pool_id != info->pool_id)
            continue;
        return info->type_mem;
    }
    return mem_type_invalid;
}

static struct bitmask* get_nodemask_from_pool_id(int pool_id){
    for(int i=0;i<fallback->nr_node;i++){ /* nr_node = nr_pool */
        info_node *info = &(fallback->list_node)[0][i];
        if (pool_id != info->pool_id)
            continue;
        return info->nodemask;
    }
    return NULL;
}

static void _init_arena_pool(int pool_id, mem_type_t type_mem){
    size_t sz = sizeof(unsigned);
    struct bitmask *nodemask;
    assert(type_mem != mem_type_invalid);

    g_arena_pool[pool_id].type_mem = type_mem;
    g_arena_pool[pool_id].nr_arena = 1;
    if (opt_smdk.use_auto_arena_scaling) {
        if (type_mem == mem_type_exmem) {
            g_arena_pool[pool_id].nr_arena = MAX(1, smdk_info.nr_arena_exmem / smdk_info.nr_pool_exmem);
        } else { /* type_mem == mem_type_normal */
            g_arena_pool[pool_id].nr_arena = MAX(1, smdk_info.nr_arena_normal / smdk_info.nr_pool_normal);
        }
    }

    nodemask = get_nodemask_from_pool_id(pool_id);
    assert(nodemask != NULL);
    g_arena_pool[pool_id].nodemask = nodemask;

    for(int i=0;i<g_arena_pool[pool_id].nr_arena;i++){
        if(je_mallctl("arenas.create", (void *)&g_arena_pool[pool_id].arena_id[i], &sz, NULL, 0)){
            fprintf(stderr,"arena_pool[%s] arena.create failure\n",(type_mem==mem_type_normal)?"normal":"exmem");
            assert(false);
        }
        arena_t* arena = arena_get(TSDN_NULL,g_arena_pool[pool_id].arena_id[i],false);
        assert(arena != NULL);
        arena_set_smdk_flag(arena, pool_id, nodemask);
    }
}

static void init_arena_pool(){
    g_arena_pool = (arena_pool *)malloc(sizeof(arena_pool)*smdk_info.nr_pool);
    assert(g_arena_pool);
    for(int i=0;i<smdk_info.nr_pool;i++)
        _init_arena_pool(i, get_type_mem_from_pool_id(i));
}

static int distance_compare(const void *_a, const void *_b){
    info_node *a = (info_node *)_a;
    info_node *b = (info_node *)_b;
    if (a->val == b->val)
        return 0;
    else if (a->val > b->val)
        return 1;
    return -1;
}

static void set_all_exmem_node_mask(struct bitmask* bmp, int nr_node_normal, int max_node){
    for(int i=nr_node_normal;i<=max_node;i++){
        if (numa_nodes_ptr && numa_bitmask_isbitset(numa_nodes_ptr, i))
            numa_bitmask_setbit(bmp, i);
    }
}

static void set_exmem_partition_range_mask(void){
    int max_node = numa_max_node();
    struct bitmask *bmp;
    if(strcmp(opt_smdk.exmem_partition_range, "")) {
        if (!strcmp(opt_smdk.exmem_partition_range, "all")) {
            /* all */
            bmp = numa_allocate_nodemask();
            assert(bmp != NULL);
            set_all_exmem_node_mask(bmp, fallback->nr_node_normal, max_node);
            smdk_info.exmem_range_bitmask = bmp;
        } else {
            /* node list */
            bmp = numa_parse_nodestring(opt_smdk.exmem_partition_range);
            if (bmp == NULL || numa_bitmask_equal(bmp, numa_no_nodes_ptr)) {
                goto err;
            }
            /* filtering out the DDR nodes */
            struct bitmask *all_exmem_node_bmp = numa_allocate_nodemask();
            assert(all_exmem_node_bmp != NULL);
            set_all_exmem_node_mask(all_exmem_node_bmp, fallback->nr_node_normal, max_node);
            for (int i = 0 ; i <=max_node; i++) {
                if (numa_bitmask_isbitset(bmp, i)) {
                    if (numa_bitmask_isbitset(all_exmem_node_bmp,i) == 0) {
                        numa_bitmask_clearbit(bmp, i);
                    }
                }
            }
            if (numa_bitmask_equal(bmp, numa_no_nodes_ptr)) {
                smdk_info.exmem_range_bitmask = NULL;
                goto err;
            } else {
                smdk_info.exmem_range_bitmask = bmp;
            }
            numa_free_nodemask(all_exmem_node_bmp);
        }
    }
    return;

err:
    fprintf(stderr, "[Warning] Invalid value for \"exmem_partition_range\":\"%s\"."
           " This option will be ignored.\n", opt_smdk.exmem_partition_range);

}

void init_fallback_order(void){
    int from, to, start_node_exmem, max_node;
    int nr_node_normal = 0;
    int nr_node_exmem = 0;
    char path[MAX_CHAR_LEN] = {0, };
    char has_cpu[MAX_CHAR_LEN] = {0, };
    char has_memory[MAX_CHAR_LEN] = {0, };
    FILE *file;
    struct bitmask *bmp_has_cpu, *bmp_has_memory;

    cpumap = (int *)malloc(sizeof(int)*numa_num_configured_cpus());
    assert(cpumap);

    sprintf(path, "/sys/devices/system/node/has_cpu");
    file = fopen(path, "r");
    assert(file != NULL);
    fgets(has_cpu, MAX_CHAR_LEN, file);
    strtok(has_cpu, "\n");
    fclose(file);

    sprintf(path, "/sys/devices/system/node/has_memory");
    file = fopen(path, "r");
    assert(file != NULL);
    fgets(has_memory, MAX_CHAR_LEN, file);
    strtok(has_memory, "\n");
    fclose(file);

    bmp_has_cpu = numa_parse_nodestring(has_cpu);
    assert(bmp_has_cpu != NULL && bmp_has_cpu != numa_no_nodes_ptr);

    bmp_has_memory = numa_parse_nodestring(has_memory);
    assert(bmp_has_memory != NULL && bmp_has_memory != numa_no_nodes_ptr);

    max_node = numa_max_node();
    for (int nid=0;nid<=max_node;nid++) {
        if ((numa_bitmask_isbitset(bmp_has_cpu,nid)) && (numa_bitmask_isbitset(bmp_has_memory,nid))) {
            /* ddr node */
            nr_node_normal++;
        } else if (numa_bitmask_isbitset(bmp_has_memory,nid)) {
            /* exmem node */
            nr_node_exmem++;
        } else {
            /* do nothing */
        }
    }

    for (int i=0;i<numa_num_configured_cpus();i++){
        cpumap[i] = numa_node_of_cpu(i);
        // fprintf(stderr, "cpumap[%d]: %d\n", i, cpumap[i]);
    }

    /* init smdk_fallback */
    fallback = (smdk_fallback *)malloc(sizeof(smdk_fallback));
    assert(fallback);

    /* check exmem partition range configuration */
    fallback->nr_node_normal = nr_node_normal;
    fallback->nr_node_exmem = nr_node_exmem;
    set_exmem_partition_range_mask();

    if (smdk_info.exmem_range_bitmask != NULL) {
        fallback->nr_node_exmem = 1;
    }
    fallback->nr_node = fallback->nr_node_normal + fallback->nr_node_exmem;  /* the number of memory nodes in the system */

    fallback->list_node = (info_node **)malloc(sizeof(info_node *)*nr_node_normal);
    assert(fallback->list_node);

    for(from=0;from<fallback->nr_node_normal;from++){
        int to_node = 0;
        (fallback->list_node)[from] = (info_node *)malloc(sizeof(info_node)*fallback->nr_node);
        assert((fallback->list_node)[from]);
        for(to=0;to<fallback->nr_node;to++){
            info_node *info = &(fallback->list_node)[from][to];
            info->pool_id = to;
            while(!numa_bitmask_isbitset(numa_nodes_ptr, to_node++));

            if (to >= fallback->nr_node_normal && smdk_info.exmem_range_bitmask) {
                info->type_mem = mem_type_exmem;
                info->val = MAX_NUMA_DISTANCE;
                info->nodemask = smdk_info.exmem_range_bitmask;
                info->node_id = -1;
            } else {
                if (to < fallback->nr_node_normal) { //mem_type_normal
                    info->type_mem = mem_type_normal;
                } else {
                    info->type_mem = mem_type_exmem;
                }
                info->val = numa_distance(from, to_node-1);
                info->nodemask = numa_allocate_nodemask();
                assert(info->nodemask != NULL);
                numa_bitmask_setbit(info->nodemask, to_node-1);
                info->node_id = to_node-1;
            }
        }
    }

    /* sort */
    start_node_exmem = fallback->nr_node_normal;
    for(from=0;from<fallback->nr_node_normal;from++){
        info_node *list_node = fallback->list_node[from];
        qsort((void *)list_node, fallback->nr_node_normal, sizeof(info_node), distance_compare);
        if (smdk_info.exmem_range_bitmask == NULL)
            qsort((void *)(list_node + start_node_exmem), fallback->nr_node_exmem, sizeof(info_node), distance_compare);
    }
}

static void init_smdk_info(void) {
    if(opt_smdk.use_auto_arena_scaling){
        smdk_info.get_target_arena = get_auto_scale_target_arena;
    }else{
        smdk_info.get_target_arena = get_normal_target_arena;
    }
    init_smdk_rwlock(&smdk_info.rwlock_current_prio);

    init_smdk_rwlock(&smdk_info.alloc_mgmt[mem_type_normal].rwlock_size_alloc);
    init_smdk_rwlock(&smdk_info.alloc_mgmt[mem_type_exmem].rwlock_size_alloc);
    smdk_info.alloc_mgmt[mem_type_normal].size_alloc = 0;
    smdk_info.alloc_mgmt[mem_type_exmem].size_alloc = 0;
    smdk_info.current_prio = 0;
    smdk_info.maxmemory_policy = opt_smdk.maxmemory_policy;
    smdk_info.nr_pool = fallback->nr_node; /* numa_max_node() + 1; */
    smdk_info.nr_pool_normal = fallback->nr_node_normal;
    smdk_info.nr_pool_exmem = fallback->nr_node_exmem;
}

int init_smdk(void){
    if (smdk_info.smdk_initialized)
        return SMDK_RET_SUCCESS;

    if (opt_smdk.adaptive_interleaving_policy == policy_weighted_interleaving)
        return SMDK_RET_SUCCESS;

    if (opt_smdk.use_exmem == true) {
        scale_arena_pool();
        init_fallback_order();
        init_smdk_info();
        init_arena_pool();
        smdk_info.smdk_initialized = true;

        return SMDK_RET_SUCCESS;
    } else {
        return SMDK_RET_USE_EXMEM_FALSE;
    }
}

void terminate_smdk(void){
    if ((opt_smdk.adaptive_interleaving_policy == policy_weighted_interleaving)
        || (smdk_info.smdk_initialized == false))
        return;

    for(int from=0;from<fallback->nr_node_normal;from++){
        for(int to=0;to<fallback->nr_node;to++){
            info_node *info = &(fallback->list_node)[from][to];
            if (info->type_mem == mem_type_exmem && smdk_info.exmem_range_bitmask != NULL)
                continue;
            numa_free_nodemask(info->nodemask);
        }
        if((fallback->list_node)[from])
            free((fallback->list_node)[from]);
    }

    if (smdk_info.exmem_range_bitmask != NULL)
        numa_free_nodemask(smdk_info.exmem_range_bitmask);

    if (fallback->list_node)
        free(fallback->list_node);
    if (g_arena_pool)
        free(g_arena_pool);
    if (cpumap)
        free(cpumap);
    if (fallback)
        free(fallback);
}
