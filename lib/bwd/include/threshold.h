#ifndef __BWD_THRESHOLD_H__
#define __BWD_THRESHOLD_H__

extern pthread_mutex_t mutex;
extern pthread_cond_t cond;
extern pthread_mutex_t sync_mutex;
extern pthread_cond_t sync_cond;

extern bool threshold_fin;
extern bool threshold_created;

extern int spike[NUMA_NUM_NODES];
extern int head;
extern int stop_event;

extern char mlc_file_path[MAX_PATH_LEN];

void *threshold_func(void *args __attribute__((unused)));

int kill_threshold_child();

#endif
