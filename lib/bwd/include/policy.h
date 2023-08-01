#ifndef __BWD_POLICY_H__
#define __BWD_POLICY_H__

extern int max_window;
extern int head;

extern char **output_file_names;

void *policy_func(void *args __attribute__((unused)));

#endif
