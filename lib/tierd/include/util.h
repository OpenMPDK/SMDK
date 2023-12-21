#ifndef __TIERD_UTIL_H__
#define __TIERD_UTIL_H__

#include <stdio.h>
#include <syslog.h>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define GETTIME(begin, end) \
        ((end.tv_sec - begin.tv_sec) + \
            (end.tv_nsec - begin.tv_nsec) / 1000000000.0)

typedef enum {
	SYSLOG_LEVEL_ERROR,
	SYSLOG_LEVEL_INFO,
	SYSLOG_LEVEL_DEBUG
} tierd_loglevel;

#define tierd_error(...)		tierdlog(SYSLOG_LEVEL_ERROR, __VA_ARGS__)
#define tierd_info(...)			tierdlog(SYSLOG_LEVEL_INFO, __VA_ARGS__)
#define tierd_debug(...)		tierdlog(SYSLOG_LEVEL_DEBUG, __VA_ARGS__)

int log_is_on_stderr(void);
void log_init(int on_stderr);
void tierdlog(tierd_loglevel level, const char *fmt, ...);

#define EPS 1e-10

#define NODE_DIR_LEN	2048
#define MAX_PATH_LEN	4096
#define MAX_CMD_LEN     (MAX_PATH_LEN + 512)

#define INTERVAL 1000000 // in us

// Initial dummy value
#define SPIKE 100000.0f

#define MAX_WINDOW 4

#define TIERD_DEV_PATH      "/dev/tierd"
#define TIERD_DIR          "/run/tierd/" // Using tmpfs as backend
#define CONFIG_FILE_PATH    "tierd.conf"
#define MLC_FILENAME        "mlc"
#ifdef ARCH_AMD
#define AMD_UPROFPCM_FILENAME        "AMDuProfPcm"
#endif

#define TIERD_REGISTER_TASK     _IO('M', 0)

enum bandwidth_type {
    BW_READ,
    BW_WRITE,
    BW_RDWR,
    MAX_NR_BW_TYPE
};

#define IDLE_STATE_BW_FILE "idle_state_bandwidth"
#define IDLE_STATE_CAPA_FILE "idle_state_capacity"
#define MAX_READ_BW_FILE "max_read_bw"
#define MAX_WRITE_BW_FILE "max_write_bw"
#define MAX_BW_FILE "max_bw"
#define FALLBACK_ORDER_BW_FILE "fallback_order_bandwidth"
#define FALLBACK_ORDER_TIER_FILE "fallback_order_tier"

#endif
