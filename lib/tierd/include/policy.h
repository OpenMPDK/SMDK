#ifndef __TIERD_POLICY_H__
#define __TIERD_POLICY_H__

extern int max_window;
extern int head;

extern char **node_dir_path;
extern char state_f[][64];
extern int state_f_num;

void *policy_func(void *args __attribute__((unused)));

#endif
