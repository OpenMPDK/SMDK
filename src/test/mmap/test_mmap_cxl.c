#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* Note: MAP_EXMEM must be identical with MAP_EXMEM at linux-5.17-rc5-smdk/include/uapi/asm-generic/mman-common.h */
#ifndef MAP_EXMEM
#define MAP_EXMEM 0x200000
#endif

int main(int argc, char* argv[]) {
	unsigned long size = 4*1024*1024;
	unsigned int flag = MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_POPULATE;

	if(argc > 2){
		fprintf(stderr,"Usage: 'test_mmap_cxl ZONE', [ZONE=e|E : ZONE_EXMEM, otherwise ZONE_NORMAL]\n");
		exit(1);
	}

	if(argc == 2 && (*argv[1] == 'e' || *argv[1] == 'E')){
		flag |= MAP_EXMEM;
		fprintf(stderr,"MAP_EXMEM\n");
	}

	while (1) {
		char *addr = mmap(NULL, size, PROT_READ|PROT_WRITE, flag, -1, 0);
		char one,zero;
		if (addr == MAP_FAILED) {
			perror("mmap error");
			exit(1);
		}

		memset(addr,'1',size);
		one=*(addr+size/2);
		memset(addr,'0',size);
		zero=*(addr+size/2);
		printf("addr[%p], one='%c' zero='%c'\n", (void *)addr, one, zero);

#if 0
		if (munmap(tmp, 1024) == -1) {
			perror("munmap error");
			exit(1);
		}
#endif
		sleep(1);
	}

	return 0;
}

