#ifndef __BWD_UTIL_H__
#define __BWD_UTIL_H__

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
} bwd_loglevel;

#define bwd_error(...)		bwdlog(SYSLOG_LEVEL_ERROR, __VA_ARGS__)
#define bwd_info(...)		bwdlog(SYSLOG_LEVEL_INFO, __VA_ARGS__)
#define bwd_debug(...)		bwdlog(SYSLOG_LEVEL_DEBUG, __VA_ARGS__)

int log_is_on_stderr(void);
void log_init(int on_stderr);
void bwdlog(bwd_loglevel level, const char *fmt, ...);

#define MAX_PATH_LEN    4096
#define MAX_CMD_LEN     (MAX_PATH_LEN + 512)

#define INTERVAL 500000 // in us

// Initial dummy value
#define SPIKE 100000.0f

#define MAX_WINDOW 5

#define BWD_DEV_PATH      "/dev/bwd"
#define BWD_DIR          "/run/bwd/" // Using tmpfs as backend
#define CONFIG_FILE_PATH    "bwd.conf"
#define MLC_FILENAME        "mlc"
#ifdef ARCH_AMD
#define AMD_UPROFPCM_FILENAME        "AMDuProfPcm"
#endif

#define BWD_REGISTER_TASK     _IO('M', 0)

enum bandwidth_type {
    BW_READ,
    BW_WRITE,
    BW_RDWR,
    MAX_NR_BW_TYPE
};

#endif
