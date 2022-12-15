#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<signal.h>
#include<stdbool.h>
#include<string.h>

int fail = 0;

void malloc_test(){
    void* buffer;
    size_t size=0x400000;
    buffer = malloc(size);
    memset(buffer,'0',size);
    printf("malloc: %p\n", buffer);
    if (buffer != NULL) {
        printf("free: %p\n", buffer);
        free(buffer);
    } else {
        printf("malloc fail\n");
        fail = 1;
    }
    return;
}

void calloc_test(){
    void* buffer;
    size_t size=0x400000;
    buffer = calloc(1,size);
    printf("calloc: %p\n", buffer);
    if (buffer != NULL) {
        printf("free: %p\n", buffer);
        free(buffer);
    } else {
        printf("calloc fail\n");
        fail = 1;
    }
    return;
}

void realloc_test(){
    void* buffer;
    size_t size=0x400000;
    buffer = malloc(size);
    memset(buffer,'0',size);
    printf("malloc: %p\n", buffer);
    if (buffer == NULL) {
        printf("malloc(realloc) error\n");
        fail = 1;
        return;
    }

    size_t newsize=0x800000;
    buffer = realloc(buffer, newsize);
    printf("realloc: %p\n", buffer);
    if (buffer != NULL) {
        printf("free: %p\n", buffer);
        free(buffer);
    } else {
        printf("realloc error\n");
        fail = 1;
    }
    return;
}

void posix_memalign_test(){
    void* buffer;
    int ret;
    ret = posix_memalign(&buffer, sizeof(void*), 8);
    if (ret != 0) {
        printf("posix_memalign fail(ret=%d)\n", ret);
        fail = 1;
    } else {
        printf("posix_memalign: %p\n", buffer);
        printf("free: %p\n", buffer);
        free(buffer);
    }
    return;
}

int main(){
    malloc_test();
    calloc_test();
    realloc_test();
    posix_memalign_test();

    if(fail != 0)
        return 1;

    return 0;
}

