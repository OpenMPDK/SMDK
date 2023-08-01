#ifndef __BWD_DAEMON_H__
#define __BWD_DAEMON_H__

#include "util.h"

extern void *monitor_func(void *args __attribute__((unused)));
extern void *threshold_func(void *args __attribute__((unused)));
extern void *policy_func(void *args __attribute__((unused)));

extern int kill_threshold_child();

extern bool threshold_exit;

#endif
