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

#include "core/include/internal/init.h"
#include "core/include/internal/config.h"
#include "internal/meta_api.h"

int init_smalloc(void) {
    smdk_init_helper("SMALLOC_CONF", true);
    int ret = init_smdk();
    if (ret == SMDK_RET_SUCCESS) {
        init_system_mem_size();
        smdk_info.smdk_initialized = true;
    }
    return ret;
}

void terminate_smalloc(void) {
    if (smdk_info.smdk_initialized == false)
        return;

    terminate_smdk();
    free(smdk_info.stats_per_node);
}

void intersect_bitmask(mem_type_t type, struct bitmask *nodemask) {
    int node_start = (type == mem_type_normal) ? smdk_info.nr_pool_normal : 0;

    for (int node = node_start; node <= numa_max_node(); node++) {
        if (!numa_bitmask_isbitset(nodemask, node))
            continue;
        if (type == mem_type_normal) {
            /* type = normal, bitmask = exmem */
            numa_bitmask_clearbit(nodemask, node);
        } else {
            if ((node >= smdk_info.nr_pool_normal) && numa_bitmask_isbitset(numa_nodes_ptr, node))
                continue;
            /* type = exmem, bitmask = normal or invalid exmem */
            numa_bitmask_clearbit(nodemask, node);
        }
    }
}
