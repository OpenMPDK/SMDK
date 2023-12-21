#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <numa.h>

#include "smdk_opt_api.h"

#define MAX_NUM_THREADS (100)

size_t size = 64*1024*1024;
int iter = 10;
int nthreads=1;
char* node = "1";

bool is_node_valid(char *node) {
    if (numa_parse_nodestring(node) == NULL) {
        printf("Invalid node(s): %s\n", node);
        return false;
    }
    return true;
}

void* thd_start(void *arg) {
    void* ptr1;
    int thread_num = (*(int *)arg)+1;
    printf("thread%d malloc test start\n",thread_num);

    for(int i=0; i<iter; i++){
        ptr1 = s_malloc_node(SMDK_MEM_EXMEM, size, node);
        assert(ptr1);
        s_free_node(SMDK_MEM_EXMEM,ptr1,size/2);
    }


    s_stats_print('g');
    size_t mem_used = s_get_memsize_used(SMDK_MEM_EXMEM);
    printf("mem used : %zu\n",mem_used);
    printf("thread%d malloc test over\n",thread_num);
    return NULL;
}

void test_per_thread(int nthreads) {
    int i;
    int status;
    pthread_t test_threads[MAX_NUM_THREADS]= {0,};

    for (i = 0; i < nthreads; i++) {
        pthread_create(&test_threads[i], NULL, thd_start, (void *)&i);
        fprintf(stderr,"create- thread%d\n",i+1);
        usleep(100000);
    }
    s_stats_print('G');
    for(int j=nthreads-1; j>=0; j--){
        int ret = pthread_join(test_threads[j], (void**)&status);
        assert(ret == 0);
    }
}

int main(int argc, char* argv[]) {
    for(int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "size")) {
            size = (size_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "iter")) {
            iter = (int)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "nthreads")) {
            nthreads = (int)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "node")) {
            node = argv[++i];
            if (!is_node_valid(node))
                return 2;
        } else {
            printf("\n[TEST ERROR] argv[%d]: %s, Please check user input\n\
                    e.g) size xx iter xx node 1,3 nthreads 3 \n", i, argv[i]);
            return 2;
        }
    }

    printf("[TEST START] smdk exmem node specific allocation test\n");
    printf("[TEST PARAMETERS] nodes=%s size=%ld iter=%d nthreads=%d\n",
           node, size, iter, nthreads);
    test_per_thread(nthreads);
    s_stats_print('G');
}
