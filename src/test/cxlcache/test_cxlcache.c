#include "util.h"

int put_get_correctness()
{
	u64 before_put = 0, after_put = 0, put_pages;
	u64 before_get = 0, after_get = 0, get_pages;
	char *filepath;
	int ret = 0;

	/* Prepare test */

	/* Create test file */
	filepath = create_file(0);
	if (filepath == NULL) {
		fprintf(stderr, "File create fail\n");
		ret = ENV_SET_FAIL;
		goto end;
	}

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('3');
	if (ret != 0)
		goto end;

	/* Read for set PG_mappedtodisk */
	ret = read_file_seq(filepath);
	if (ret != 0)
		goto end;

	/* Test start */

	/* Get put,get count before test */
	get_succ_puts(&before_put);
	get_succ_gets(&before_get);

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('1');
	if (ret != 0)
		goto end;

	/* Read occur and cxlcache get occur at this point */
	ret = read_file_seq(filepath);
	if (ret != 0) {
		goto end;
	}

	/* Get put,get count after test */
	get_succ_puts(&after_put);
	get_succ_gets(&after_get);

	put_pages = (after_put - before_put) * 4096;
	get_pages = (after_get - before_get) * 4096;

	/* Test end, summray */
	if (put_pages == 0 || get_pages == 0) {
		fprintf(stderr, "Maybe there's no put/get from/to CXL memory\n");
		return ENV_SET_FAIL;
	}

	result_summary(put_pages, get_pages);

end:
	/* Remove file created before test */
	remove_file_if_exist(filepath);
	free(filepath);

	return ret;
}

int modify_put_get_correctness()
{
	u64 before_put = 0, after_put = 0, put_pages;
	u64 before_get = 0, after_get = 0, get_pages;
	char *filepath;
	int ret = 0;

	/* Prepare test */

	/* Create test file */
	filepath = create_file(0);
	if (filepath == NULL) {
		fprintf(stderr, "File create fail\n");
		ret = ENV_SET_FAIL;
		goto end;
	}

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('3');
	if (ret != 0)
		goto end;

	/* Read for set PG_mappedtodisk */
	ret = read_file_seq(filepath);
	if (ret != 0) {
		goto end;
	}

	/* Test start */

	/* Get put,get count before test */
	get_succ_puts(&before_put);
	get_succ_gets(&before_get);

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('1');
	if (ret != 0)
		goto end;

	/* Read occur and cxlcache get occur at this point */
	ret = read_file_seq(filepath);
	if (ret != 0)
		goto end;

	ret = write_file_rev(filepath);
	if (ret != 0)
		goto end;

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('1');
	if (ret != 0)
		goto end;

	ret = read_file_rev(filepath);
	if (ret != 0)
		goto end;

	/* Get put,get count after test */
	get_succ_puts(&after_put);
	get_succ_gets(&after_get);

	put_pages = (after_put - before_put) * 4096;
	get_pages = (after_get - before_get) * 4096;

	/* Test end, summray */


	if (put_pages == 0 || get_pages == 0) {
		fprintf(stderr, "Maybe there's no put/get from/to CXL memory\n");
		return ENV_SET_FAIL;
	}

	result_summary(put_pages, get_pages);

end:
	/* Remove file created before test */
	remove_file_if_exist(filepath);
	free(filepath);

	return ret;
}

int multi_thread()
{
	u64 before_put = 0, after_put = 0, put_pages;
	u64 before_get = 0, after_get = 0, get_pages;
	double start, end;
	int ret = 0;
	pthread_t *tid_arr;
	thread_data_t *td_arr;

	/* Prepare test */
	tid_arr = calloc(MAX_THREADS, sizeof(pthread_t));
	td_arr = calloc(MAX_THREADS, sizeof(thread_data_t));

	/* Create test file */
	for (int i = 0; i < num_threads; i++) {
		td_arr[i].path = create_file(i);
		if (td_arr[i].path == NULL) {
			fprintf(stderr, "File create fail\n");
			ret = ENV_SET_FAIL;
			goto end;
		}
	}

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('3');
	if (ret != 0)
		goto end;

	/* Read for set PG_mappedtodisk */
	for (int i = 0; i < num_threads; i++) {
		ret = read_file_seq(td_arr[i].path);
		if (ret != 0)
			goto end;
	}

	get_succ_puts(&before_put);
	get_succ_gets(&before_get);

	for (int i = 0; i < num_threads; i++) {
		if (pthread_create(&tid_arr[i], NULL, modify_put, (void *)&td_arr[i]) != 0) {
			ret = ENV_SET_FAIL;
			goto end;
		}
	}

	for (int i = 0; i < num_threads; i++) {
		if (pthread_join(tid_arr[i], NULL) != 0) {
			ret = ENV_SET_FAIL;
			goto end;
		}
	}

	for (int i = 0; i < num_threads; i++) {
		if (td_arr[i].ret != 0) {
			ret = td_arr[i].ret;
			goto end;
		}
	}

	if (ret != 0)
		goto end;

	/* Get put,get count after test */
	get_succ_puts(&after_put);
	get_succ_gets(&after_get);

	put_pages = (after_put - before_put) * 4096;
	get_pages = (after_get - before_get) * 4096;

	/* Test end, summray */
	result_summary(put_pages, get_pages);

end:
	/* Remove file created before test */
	for (int i = 0; i < num_threads; i++) {
		remove_file_if_exist(td_arr[i].path);
		free(td_arr[i].path);
	}
	free(tid_arr);
	free(td_arr);

	return ret;
}

int multi_process()
{
	u64 before_put = 0, after_put = 0, put_pages;
	u64 before_get = 0, after_get = 0, get_pages;
	char *filepath;
	int status;
	int ret = 0;

	/* Prepare test */

	/* Create test file */
	filepath = create_file(0);
	if (filepath == NULL) {
		fprintf(stderr, "File create fail\n");
		ret = ENV_SET_FAIL;
		goto end;
	}

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('3');
	if (ret != 0)
		goto end;

	/* Read for set PG_mappedtodisk */
	ret = read_file_seq(filepath);
	if (ret != 0) {
		goto end;
	}

	/* Test start */

	/* Get put,get count before test */
	get_succ_puts(&before_put);
	get_succ_gets(&before_get);

	pid_t c_pid = fork();
	if (c_pid < 0) {
		perror("fork failed");
		return ENV_SET_FAIL;
	}

	if (c_pid == 0) {
		/* Drop page cache arise and cxlcache put occur at this point */
		ret = drop_caches('1');
		if (ret != 0)
			goto end;

		/* Read occur and cxlcache get occur at this point */
		ret = read_file_seq(filepath);
		if (ret != 0) {
			goto end;
		}

		ret = write_file_rev(filepath);
		if (ret != 0)
			goto end;

		/* Drop page cache arise and cxlcache put occur at this point */
		ret = drop_caches('1');
		if (ret != 0)
			goto end;

		exit(TEST_SUCCESS);
	}

	if (waitpid(c_pid, &status, 0) != c_pid)
		return ENV_SET_FAIL;

	if (status != 0)
		return WEXITSTATUS(status);

	ret = read_file_rev(filepath);
	if (ret != 0) {
		goto end;
	}

	/* Get put,get count after test */
	get_succ_puts(&after_put);
	get_succ_gets(&after_get);

	put_pages = (after_put - before_put) * 4096;
	get_pages = (after_get - before_get) * 4096;

	/* Test end, summray */


	if (put_pages == 0 || get_pages == 0) {
		fprintf(stderr, "Maybe there's no put/get from/to CXL memory\n");
		return ENV_SET_FAIL;
	}

	result_summary(put_pages, get_pages);

end:
	/* Remove file created before test */
	remove_file_if_exist(filepath);
	free(filepath);

	return ret;
}

int put_cxl_page()
{
	u64 before_put = 0, after_put = 0, put_pages;
	u64 before_get = 0, after_get = 0, get_pages;
	char *filepath;
	int status;
	struct bitmask *mem_mask_orig, *mem_mask;
	int ret = 0;

	/* Prepare test */

	/* prepare nodemask for membind */
	mem_mask_orig = numa_get_membind();
	mem_mask = numa_allocate_nodemask();
	numa_bitmask_setbit(mem_mask, noop_node);

	/* Create test file */
	filepath = create_file(0);
	if (filepath == NULL) {
		fprintf(stderr, "File create fail\n");
		ret = ENV_SET_FAIL;
		goto end;
	}

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('3');
	if (ret != 0)
		goto end;

	/* Read for set PG_mappedtodisk */
	ret = read_file_seq(filepath);
	if (ret != 0) {
		goto end;
	}

	/* Test start */

	/* Get put,get count before test */
	get_succ_puts(&before_put);
	get_succ_gets(&before_get);

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('1');
	if (ret != 0)
		goto end;

	/* Set membind at CXL only node for using CXL by page cache */
	numa_set_membind(mem_mask);

	/* Read test file by CXL Memory*/
	ret = read_file_seq(filepath);
	if (ret != 0) {
		goto end;
	}

	ret = write_file_rev(filepath);
	if (ret != 0)
		goto end;

	/* Drop page cache arise and cxlcache put occur at this point */
	ret = drop_caches('1');
	if (ret != 0)
		goto end;

	/* Restore membind */
	numa_set_membind(mem_mask_orig);

	ret = read_file_rev(filepath);
	if (ret != 0) {
		goto end;
	}

	/* Get put,get count after test */
	get_succ_puts(&after_put);
	get_succ_gets(&after_get);

	put_pages = (after_put - before_put) * 4096;
	get_pages = (after_get - before_get) * 4096;

	/* Test end, summray */

	result_summary(put_pages, get_pages);

end:
	/* Remove file created before test */
	remove_file_if_exist(filepath);
	numa_free_nodemask(mem_mask);
	free(filepath);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret;
	int need;
	int tc_num = 0;

	if (!strcmp(argv[2], "multi_thread"))
		tc_num = 1;
	else if (!strcmp(argv[2], "put_cxl_page"))
		tc_num = 2;

	need = (tc_num)? 5: 4;

	if (argc != need)
		INPUT_ERR();

	if (parse(argv[1], argv[2]))
		INPUT_ERR();

	if (strlen(argv[3]) > PATH_MAX)
		INPUT_ERR();
	strcpy(test_path, argv[3]);

	if (tc_num == 1)
		num_threads = atoi(argv[4]);
	else if (tc_num == 2)
		noop_node = atoi(argv[4]);

	pid = getpid();

	ret = get_is_system_has_cxlcache();
	if (ret == 0) {
		fprintf(stderr, "Your System doesn't have CXL Cache \n");
		fprintf(stderr, "Build SMDK with CONFIG_CXLCACHE=y \n");
		return ENV_SET_FAIL;
	} else if (ret == -1) {
		fprintf(stderr, "Read CXL Cache Status Error \n");
		fprintf(stderr, "Check your Kernel Config Files \n");
		return ENV_SET_FAIL;
	}

	if (get_cxlcache_enable_status() == -1) {
		fprintf(stderr, "Read CXL Cache Status Erorr \n");
		fprintf(stderr, "Check CXL Cache Module Loaded \n");
		return ENV_SET_FAIL;
	}

	if (num_threads > MAX_THREADS) {
		fprintf(stderr, "The number of threads should be %d or less\n", MAX_THREADS);
		return ENV_SET_FAIL;
	}

	/* Test Body */
	print_test_info();
	ret = test_func();
	printf(ret == 0 ? "====== PASS ======\n" :
			ret == 1 ? "====== FAIL ======\n" : "==== ENV SET FAIL ====\n");

	return ret;
}
