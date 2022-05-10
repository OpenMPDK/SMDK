#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>
#define MAX_NUM_THREADS (100)

size_t size = 1024;
int iter = 1024*1024;
int nthreads=10;

void* thd_start(void *arg) {
    int threadnum = (*(int *)arg)+1;
    printf("thread %d start\n",threadnum);

    void* ptr1;
    for(int i=0; i<iter; i++){
        ptr1 = malloc(size);
        assert(ptr1);
        memset(ptr1,0,size);
        //printf("malloc: %p, thread : %d\n", ptr1,threadnum);
    }
    return NULL;
}

void test_per_thread() {
    int j;
    int status;
    pthread_t test_threads[MAX_NUM_THREADS]={0,};

    for(j=0; j<nthreads; j++){
        pthread_create(&test_threads[j], NULL, thd_start, (void *)&j);
	usleep(100000);
    }
    for(j=nthreads-1; j>=0; j--){
        int ret = pthread_join(test_threads[j], (void**)&status);
        assert(ret == 0);
        printf("thread %d done\n",j+1);
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
        } else {
            printf("\n[TEST ERROR] argv[%d]: %s, Please check user input\n\
                    e.g) size xx iter xx nthreads 3 \n", i, argv[i]);
            return -1;
        }
    }
    printf("[TEST START] smdk compatible API multi-thread malloc test\n");
    printf("[TEST PARAMETERS] size=%ld iter=%d nthreads=%d\n",
           size, iter, nthreads);
    test_per_thread();
}

