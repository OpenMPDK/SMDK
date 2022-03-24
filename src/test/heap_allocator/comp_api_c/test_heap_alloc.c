#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<signal.h>
#include<stdbool.h>
#include<string.h>

void malloc_test(){
	void* buffer;
	size_t size=0x400000;
	buffer = malloc(size);
	memset(buffer,'0',size);
	printf("mallloc: %p\n", buffer);
	if (buffer != NULL) {
		printf("free: %p\n", buffer);
		free(buffer);
	} else {
		printf("malloc fail\n");
	}
	return;
}

void calloc_test(){
	void* buffer;
	size_t size=0x400000;
	buffer = calloc(1,size);
	printf("callloc: %p\n", buffer);
	if (buffer != NULL) {
        printf("free: %p\n", buffer);
        free(buffer);
	} else {
		printf("calloc fail\n");
	}
	return;
}

void realloc_test(){
	void* buffer;
	size_t size=0x400000;
	buffer = malloc(size);
	memset(buffer,'0',size);
	printf("mallloc: %p\n", buffer);
	if (buffer == NULL) {
		printf("malloc(realloc) error\n");
		return;
	}

	size_t newsize=0x800000;
	buffer = realloc(buffer, newsize);
	printf("reallloc: %p\n", buffer);
	if (buffer != NULL) {
	    printf("free: %p\n", buffer);
	    free(buffer);
	} else {
		printf("realloc error\n");
	}
	return;
}

void posix_memalign_test(){
	void* buffer;
	int ret;
	ret = posix_memalign(&buffer, sizeof(void*), 8);
	if(ret!=0){
		printf("posix_memalign fail(ret=%d)\n", ret);
	}
	else{
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
	return 0;
}

