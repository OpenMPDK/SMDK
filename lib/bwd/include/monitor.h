#ifndef __BWD_MONITOR_H__
#define __BWD_MONITOR_H__

#ifdef ARCH_INTEL
extern long pcm_monitor_init();
extern void pcm_monitor_start();
extern void pcm_monitor_stop();
extern int pcm_monitor_get_BW(int node_id, float *read, float *write);
#endif

#ifdef ARCH_AMD
extern char amd_uprofpcm_file_path[MAX_PATH_LEN];
#endif

extern int spike[NUMA_NUM_NODES];
extern int max_window;

void *monitor_func(void *args __attribute__((unused)));

#endif
