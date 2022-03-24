#include "test_lazy_alloc.h"

size_t mem_available_normal[max_phase];
size_t mem_available_ex[max_phase];

sem_t start_sem;
sem_t end_sem;
pthread_t threads[MAX_NR_THREADS];
int num_threads = 0;
int hmp_test_done = 0;

unsigned int mmap_flags[] = {
	(MAP_EXMEM|MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE),
	(MAP_EXMEM|MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_POPULATE),
};

#define array_len(x) (sizeof(x)/sizeof(*(x)))

char type = 'n';
int test_ex = 1;
//default pre_size = 32MB;
unsigned int pre_size = 32;

void get_short_opts(struct option *o, char *s)
{
    *s++ = '+';
    while (o->name) {
        if (isprint(o->val)) {
            *s++ = o->val;
            if (o->has_arg)
                *s++ = ':';
        }
        o++;
    }
    *s = '\0';
}

struct option opts[] = {
	{"type",1,0,'t'},
	{"prepare",1,0,'p'},
	{0}
};

static size_t get_mem_stats_available(mem_zone_t zone) {
	FILE *fs;
	char str[MAX_CHAR_LEN];
	char* zone_name;
	char* freepages;
	char* zone_name_target[2] = {NAME_NORMAL_ZONE, NAME_EX_ZONE};
	size_t memsize_available=0;
	long page_sz = sysconf(_SC_PAGESIZE);
	int i;
	fs = fopen("/proc/buddyinfo", "r");

	while(1){
		i=0;
		if(fgets(str,MAX_CHAR_LEN,fs) == NULL){
			break;
		}
		str[strlen(str) -1]='\0';
		strtok(str," ");
		strtok(NULL,",");
		strtok(NULL," ");
		zone_name = strtok(NULL," ");
		if(!strncasecmp(zone_name, zone_name_target[zone], 
					strlen(zone_name_target[zone]))){
			freepages = strtok(NULL," ");
			while(freepages!=NULL){
				memsize_available+=atoi(freepages)*(page_sz*(1<<i));
				i++;
				freepages = strtok(NULL," ");
			}
		}
	}
	fclose(fs);
	return memsize_available;
}

#ifdef PRINT_DEBUG
static char *phase_to_str(phase_t phase)
{
	switch(phase)
	{
		case before_mmap: return "Before Mmap"; break;
		case after_mmap: return "After Mmap"; break;
		case after_write: return "After Write"; break;
		case after_munmap: return "After Munmap"; break;
		case max_phase:
		default: 
			return "Invalid";
	}
}
#endif

static int check_lazy_alloc(unsigned int flag, phase_t phase)
{
	int diff = 0;
	int isit_populate = 0;
	int res = TC_PASS;

	//sanity check
	if( phase < after_mmap || phase > after_munmap )
		return TC_ERROR;

	if( test_ex ){
		diff = mem_available_ex[phase]
		- mem_available_ex[phase-1];
	} else {
		diff = mem_available_normal[phase]
		- mem_available_normal[phase-1];
	}

	if( (flag & MAP_POPULATE) == MAP_POPULATE ){
		isit_populate = 1;
	}

	switch(phase)
	{
		case after_mmap:
			if(isit_populate) {
				if(diff >= 0 ) 
					res = TC_FAIL;
			} else {
				if(diff < 0)
					res = TC_FAIL;
			}
			break;

		case after_write:
			if(isit_populate) {
				if(diff < 0)
					res = TC_FAIL;
			} else {
				if(diff >= 0)
					res = TC_FAIL;
			}
			break;

		case after_munmap:
			//TODO: Need to check munmap
			//madvise?? 
			break;

		default:
			res = TC_FAIL;
			break;
	}
	return res;
}

static void get_available_page_info(phase_t phase)
{
	mem_available_normal[phase] = get_mem_stats_available(mem_zone_normal);
	mem_available_ex[phase] = get_mem_stats_available(mem_zone_ex);

#ifdef PRINT_DEBUG
	printf("==========[%8s]==========\n", phase_to_str(phase));
	printf("%8s %15s\n", "Type","Available");
	printf("%8s %15zu\n", "Normal",mem_available_normal[phase]);
	printf("%8s %15zu\n", "Cxl",mem_available_ex[phase]);

	if( phase != before_mmap ){
		printf("-------------------------\n");
		printf("%8s %15ld\n", "DiffNorm", mem_available_normal[phase] -
				mem_available_normal[phase-1]);
		printf("%8s %15ld\n", "DiffCXL", mem_available_ex[phase] -
				mem_available_ex[phase-1]);
	}
#endif
}

//mmap test lazy alloc
int lazy_alloc(unsigned int mmap_flag, char *test_name){
		
	char *addr;
	char *i, *end;
	int res = TC_FAIL;

	//init global variable
	memset(mem_available_normal, 0x0, sizeof(mem_available_normal));
	memset(mem_available_ex, 0x0, sizeof(mem_available_ex));

	printf("[start test-case: %s]\n", test_name);

	get_available_page_info(before_mmap);

	if( test_ex ){
		mmap_flag |= MAP_EXMEM;
	}

	addr = mmap(NULL, GB_SIZE, PROT_READ|PROT_WRITE, mmap_flag, -1, 0);
	if(addr == MAP_FAILED) {
		printf("mmap error..!\n");
		exit(TC_ERROR);
	}

	get_available_page_info(after_mmap);
	res = check_lazy_alloc(mmap_flag, after_mmap);	

	//wirte to page
	end = addr + GB_SIZE;
	for(i = addr + PG_SIZE; i < end; i += PG_SIZE)
		*(char **)i = i;

	for(i = addr + PG_SIZE; i < end; i += PG_SIZE){
		if( *(char **)i != i ){
			printf("failed to read value from memory!!\n");
			exit(TC_FAIL);
		}
	}
	
	get_available_page_info(after_write);
	res |= check_lazy_alloc(mmap_flag, after_write);

	if( munmap(addr, GB_SIZE) == -1 ){
		printf("munmap error..!\n");
		exit(TC_ERROR);
	}

	get_available_page_info(after_munmap);
	res |= check_lazy_alloc(mmap_flag, after_munmap);

	return res;
}

void *stress_memory(void *arg){
	char **addr=NULL;
	int i,j;
	int tid = *((int *)arg);
	int _hmp_test_done = 0;
	static int res = TC_PASS;
	size_t sys_mem_pg = 0;
	int alloc_i;

	int flag = MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_POPULATE;
	if( test_ex ){
		flag |= MAP_EXMEM;
	}

	//sanity check
	if(tid > MAX_NR_THREADS){
		res = TC_ERROR;
		pthread_exit((void *)&res);
	}

	addr = (char **)malloc(sizeof(char *)*ARR_SIZE);
	if( !addr )
	{
		res = TC_ERROR;
		pthread_exit((void *)&res);
	}

	for(i=0; i<ARR_SIZE; i++)
	{
		//1. memory alloc
		addr[i] = mmap(NULL, GRANUALITY_SIZE, PROT_READ|PROT_WRITE,flag,-1,0);
		//2. populate page table
		for(j=0; j<GRANUALITY_SIZE; j += PG_SIZE * G_JUMP){
			addr[i][j] = 'Z';
		}

		//3. check memory corruption
		for(j=0; j<GRANUALITY_SIZE; j += PG_SIZE * G_JUMP){
			if( addr[i][j] != 'Z' )	{
				res = TC_FAIL;
				//TODO: memory unmap
				pthread_exit( (void *)&res );
			}
		}

		//4. check system memory status
		if( test_ex ){ 
			sys_mem_pg = get_mem_stats_available(mem_zone_ex);
		} else {
			sys_mem_pg = get_mem_stats_available(mem_zone_normal);
		}

		if( sys_mem_pg < pre_size * 1024 * 1024 ){
			alloc_i = i;
			break;	
		}
	}

	//increase num_threads count
	sem_wait(&start_sem);
	num_threads++;
	sem_post(&start_sem);

	do{
		sem_wait(&end_sem);
		_hmp_test_done = hmp_test_done;
		sem_post(&end_sem);
	}while(!_hmp_test_done);

	//mem dealloc
	for(i=0; i<alloc_i; i++)
	{
		munmap(addr[i],GRANUALITY_SIZE);
	}
	free(addr);

	pthread_exit((void *)&res);
}

static void create_mem_stress_thread(int n_thread)
{
	int i;
	for(i=0; i<n_thread; ++i){
		pthread_create(&threads[i], NULL, stress_memory, (void *)&i);
		usleep(100);
	}
}

static int check_mem_stress_thread(int n_thread)
{
	int i;
	void *th_res;
	int ret;
	int tc_res = TC_PASS;

	for(i=0; i<n_thread; ++i)
	{
		ret = pthread_join(threads[i], &th_res );
		if( ret != 0 ){
			tc_res = TC_ERROR;
		}
		tc_res |= (*(int *)th_res);
	}

	return tc_res;
}

int lazy_alloc_high_mem_press(unsigned int mmap_flag, char *test_name)
{
	int res = TC_PASS;
	int n_thread = 1 ;

	sem_init(&start_sem, 0, 1);
	sem_init(&end_sem, 0, 1);

	printf("[start test-case: %s]\n", test_name);
	n_thread = MAX_NR_THREADS;

	//create N thread for stress memory 
	create_mem_stress_thread(n_thread);

	while(1){
		sem_wait(&start_sem);
		if( n_thread == num_threads ){
			sem_post(&start_sem);
			break;
		}
		sem_post(&start_sem);
	}

	//TODO: change to used_mem ratio
	// use /proc/meminfo 
	usleep(3000000);
	res = lazy_alloc(mmap_flag, "hmp_lazy_alloc");

	//signal to threads that test is finished
	sem_wait(&end_sem);
	hmp_test_done = 1;
	sem_post(&end_sem);

	res |= check_mem_stress_thread(n_thread);
	return res;
}

int test_unit(int test_id, 
		int test_func(unsigned int, char *), 
		unsigned int mmap_flag,  
		char *test_name){
	int pid;
	int c_pid;
	int test_result;
	int c_test_result = -1;

	if( (pid = fork()) < 0 ){
		printf("test:%s fork error!\n", test_name);
		exit(TC_ERROR);
	}

	//child process do_test
	if( pid == 0 ){
		c_test_result = test_func(mmap_flag, test_name);
		exit(c_test_result);
	//parent process report result
	} else {
		c_pid = wait(&test_result);

		if(c_pid == -1){
			printf("Child Process wait error..!(%d)\n", errno);
			return TC_ERROR;
		}

		if(WIFEXITED(test_result)) {
			//printf("test_result:%d\n", WEXITSTATUS(test_result));
			return (WEXITSTATUS(test_result) == TC_PASS)?(TC_PASS):(TC_FAIL);
		} else if (WIFSIGNALED(test_result)){
			//lazy_alloc in high memory press
			if( test_id == 2 || test_id == 3 ){
				//if parent get sigkill from child
				if( WTERMSIG(test_result) == 9 ){
					printf("SIGKILL has occurred..!(%d)\n",
							WTERMSIG(test_result));
					return TC_PASS;
				}
			}
			//printf("status from child:%d\n",WTERMSIG(test_result));
			return TC_ERROR;
		}
	}

	return TC_ERROR;
}

int main(int argc, char* argv[]) {

	int c;
	char shortopts[array_len(opts)*2+1];
	size_t init_ex_mem=0;

	get_short_opts(opts, shortopts);
	while( (c=getopt_long(argc, argv, shortopts, opts, NULL)) != -1 ){
		switch (c) {
			case 't':
				if( strlen(optarg) != 1 || ((optarg[0] != 'n') &&
						(optarg[0] != 'e')))
				{
					printf("usage: ./test_lazy_alloc -t n or e \n");
					exit(1);
				}

				if( optarg[0] == 'e' ){
					test_ex = 1;	
				}

				if( optarg[0] == 'n' ) {
					test_ex = 0;
				}
				break;
			case 'p':
				pre_size = (unsigned int)atoi(optarg);
				break;

			default:
				break;
		}//
	}

	init_ex_mem = get_mem_stats_available(mem_zone_ex);
	if( init_ex_mem == 0 && test_ex ){
		printf("[WARNING] System has no CXL memory devices.!\n");
	}

	test test_cases[] = {
		{0, lazy_alloc, mmap_flags[0], "lazy_alloc", 
			"mmap test with lazy allocation"},
		{1, lazy_alloc, mmap_flags[1], "lazy_alloc(MAP_POPULATE)", 
			"mmap test with lazy allocation"},
		{2, lazy_alloc_high_mem_press, mmap_flags[0], 
			"lazy_alloc (with high memory press)", 
			"mmap test with high memory press"},
		{3, lazy_alloc_high_mem_press, mmap_flags[1], 
			"lazy_alloc (with high memory press, MAP_POPULATE flag)", 
			"mmap test with high memory press, map_populate"},
		{-1,NULL,0,NULL,"end ot test-case"},
	};

	for(test *pt = test_cases; pt->test_name != NULL; pt++){
		if(test_unit(pt->test_id, pt->test_func, pt->mmap_flag, pt->test_name)
				== TC_PASS){
			//unit test pass
			printf("[(%02d)test-result: %s PASS!]\n\n",
					pt->test_id, pt->test_name);
		} else {
			//unit test fail
			printf("[(%02d)test-result: %s FAIL!]\n\n",
					pt->test_id, pt->test_name);
		}
	}

	printf("\n<<TC end>>\n");

	return 0;
}

