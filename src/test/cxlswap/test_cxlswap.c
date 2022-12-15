#include "util.h"

int store_load()
{
	int before = 0, after = 0;
	volatile unsigned char *buf;

	get_stored_pages(&before);
	buf = mmap(NULL, size, PROT, FLAG, 0, 0);
	if (buf == MAP_FAILED) {
		perror("mmap");
		return ENV_SET_FAIL;
	}

	// Cold Access to buffer occurs Swap Out
	for (u64 i=0;i<size;i++) 
		buf[i] = (i & 0x7f);
		
	// Read buffer occurs Swap In
	for (u64 i=0;i<size;i++) 
		if (buf[i] != (i & 0x7f)) {
			fprintf(stderr, "Value between stored and loaded is different\n");
			return TEST_FAILURE;
		}

	get_stored_pages(&after);
    if (after == 0) {
        fprintf(stderr, "Maybe there's no swap in/out from/to ZONE_EXMEM\n");
        return ENV_SET_FAIL;
    }

	printf("======= RESULT =======\n");
	printf("CXL Swap Stored Pages Before Swap : %d \n", before);
	printf("CXL Swap Stored Pages After Swap : %d \n", after);

	return TEST_SUCCESS;
}

int multi_thread()
{
	unsigned char *buf;
	double start, end;
	int before = 0, after = 0;
	int ret = posix_memalign((void **)&buf, 4096, size);
	if (ret) {
		perror("memory allocation failed");
		return ENV_SET_FAIL;
	}

	get_stored_pages(&before);
	start = omp_get_wtime();
	// Cold Access to buffer occurs Swap Out
#pragma omp parallel for num_threads(num_threads)
	for (u64 i=0;i<size;i+=4096) 
		for(int j=0;j<4096;j++)
			buf[i+j] = ((i+j) & 0x7f);
	end = omp_get_wtime();

	// Read buffer occurs Swap In
	for (u64 i=0;i<size;i++) 
		if (buf[i] != (i & 0x7f)) {
			fprintf(stderr, "Value between stored and loaded is different\n");
			return TEST_FAILURE;
		}

	get_stored_pages(&after);
    if (after == 0) {
        fprintf(stderr, "Maybe there's no swap in/out from/to ZONE_EXMEM\n");
        return ENV_SET_FAIL;
    }

	printf("======= RESULT =======\n");
	printf("Elapsed Time %lf using %d threads\n", end - start, num_threads);
	printf("CXL Swap Stored Pages Before Swap : %d \n", before);
	printf("CXL Swap Stored Pages After Swap : %d \n", after);

	return TEST_SUCCESS;
}

int shared_memory()
{
	int shmid;
	int status;
	int before = 0, after = 0;
	unsigned char *p_shm;
	key_t key = pid % 123456;

	get_stored_pages(&before);
	if ((shmid = shmget(key, size, IPC_CREAT | 0644)) < 0) {
		perror("shm create failed");
		return ENV_SET_FAIL;
	}

	if ((p_shm = shmat(shmid, NULL, 0)) == (void *) -1) {
		perror("shm get failed");
		return ENV_SET_FAIL;
	}

	printf("Process %d Initialize Data [Shmid %d]...\n", pid, shmid);
	for (u64 i=0;i<size;i++) 
		p_shm[i] = (i & 0x7f);
	
	pid_t c_pid = fork();
	if (c_pid < 0) {
		perror("fork failed");
		return ENV_SET_FAIL;
	} 

	if (c_pid == 0) {
		unsigned char *shm; 
		if ((shmid = shmget(key, size, 0644)) < 0) 
			exit(ENV_SET_FAIL);

		if ((shm = shmat(shmid, NULL, 0)) == (void *) - 1) 
			exit(ENV_SET_FAIL);

		printf("Process %d Check Initialized Data [Shmid %d]...\n", 
															getpid(), shmid);
		for (u64 i=0;i<size;i++) {
			if(shm[i] == (i & 0x7f)) 
				continue;
			fprintf(stderr, "Value between stored and loaded is different\n");
			exit(TEST_FAILURE);
		}
		printf("Process %d Check Initialized Data [Shmid %d] Pass\n", 
															getpid(), shmid);

		printf("Process %d Modify Data [Shmid %d]...\n", getpid(), shmid);
		for (u64 i=0;i<size;i++) 
			shm[i] = ((i & 0x1) ? (i & 0xf) : (i & 0x7f));

		shmdt(shm);

		exit(TEST_SUCCESS);
	} 
	
	if(waitpid(c_pid, &status, 0) != c_pid) 
		return ENV_SET_FAIL;
	
	if (status != 0) 
		return WEXITSTATUS(status);
	
	printf("Process %d Check Modified Data [Shmid %d]...\n", pid, shmid);
	for (u64 i=0;i<size;i++) 
		if (p_shm[i] != ((i & 0x1) ? (i & 0xf) : (i & 0x7f))) {
			fprintf(stderr, "Value between stored and loaded is different\n");
			return TEST_FAILURE;
		}
	printf("Process %d Check Modified Data [Shmid %d] Pass\n", pid, shmid);
		
	shmdt(p_shm);

	get_stored_pages(&after);
    if (after == 0) {
        fprintf(stderr, "Maybe there's no swap in/out from/to ZONE_EXMEM\n");
        return ENV_SET_FAIL;
    }

	printf("======= RESULT =======\n");
	printf("CXL Swap Stored Pages Before Swap : %d \n", before);
	printf("CXL Swap Stored Pages After Swap : %d \n", after);

	return TEST_SUCCESS;
}

int main(int argc, char *argv[])
{
	int ret;
	int need;

	if (argc == 1 || argc == 3) 
		INPUT_ERR(argv[0]);	

	if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) 
		INPUT_ERR(argv[0]);	

	need = !strcmp(argv[3], "multi_thread") ? 5 : 4;

	if (argc != need) 
		INPUT_ERR(argv[0])

	if (parse(argv[1], argv[2], argv[3]))
		INPUT_ERR(argv[0])
	num_threads = need == 5 ? atoi(argv[4]) : 0;

	pid = getpid();

	ret = get_is_system_has_cxlswap();
	if (ret == 0) {
		fprintf(stderr, "Your System doesn't have CXL Swap \n");
		fprintf(stderr, "Build SMDK with CONFIG_CXLSWAP=y \n");
		return ENV_SET_FAIL;
	} else if (ret == -1) {
		fprintf(stderr, "Read CXL Swap Config Error \n");
		fprintf(stderr, "Check your Kernel Config File \n");
		return ENV_SET_FAIL;
	}

	if (get_cxlswap_enable_status() == -1) {
		fprintf(stderr, "Read CXL Swap Status Error \n");
		fprintf(stderr, "Check CXL Swap Module Loaded \n");
		return ENV_SET_FAIL;
	}

	ret = get_is_system_has_cgroup();
	if (ret == 0) {
		fprintf(stderr, "Your System doesn't have Cgroup \n");
		fprintf(stderr, "Build SMDK with CONFIG_CGROUP=y \n");
		return ENV_SET_FAIL;
	} else if (ret == -1) {
		fprintf(stderr, "Read Cgroup Config Error \n");
		fprintf(stderr, "Check your Kernel Config File \n");
		return ENV_SET_FAIL;
	}

	// Get Cgroup Version
	ret = get_cgroup_version();
	if (ret == 0) {
		cgroup_help_message();
		return ENV_SET_FAIL;
	} else if (ret == -1) {
		fprintf(stderr, "Cgroup prepare Error.\n");
		fprintf(stderr, "Mount cgroup manually\n");
		return ENV_SET_FAIL;
	}

	// Create Cgroup
	if (create_cgroup()) {
		fprintf(stderr, "Create cgroup error\n");
		return ENV_SET_FAIL;
	}

	// Avoid OOM Killer by large allocation at once
	if (avoid_oom() < 0) {
		fprintf(stderr, "Set oom_abj to avoid OOM_Killer error\n");
		ret = ENV_SET_FAIL;
		goto clean;
	}

	// Limit Process Max Memory
	if (limit_memory()) {
		fprintf(stderr, "Limit cgroup's max memory error\n");
		ret = -1;
		goto clean;
	}
	
	// Test Body
	print_test_info();
	ret = test_func();
	printf(ret == 0 ? "====== PASS ======\n" :
			ret == 1 ? "====== FAIL ======\n" : "==== ENV SET FAIL ====\n");

clean:
	// Clean Cgroup
	if (clean_env()) {
		fprintf(stderr, "Clean up used cgroup file not successfully\n");
		fprintf(stderr, "Recommend clean up (%s) by yourself\n", cgroup_child);
	}

	return ret;
}
