#include <stdarg.h>
#include "util.h"

#define MSGBUFSIZE	1024

static int log_on_stderr = 1;
static tierd_loglevel log_level = SYSLOG_LEVEL_INFO;

void log_init(int on_stderr)
{
	log_on_stderr = on_stderr ? 1 : 0;
}

int log_is_on_stderr(void)
{
	return log_on_stderr;
}

static void tierdlogv(tierd_loglevel level, const char *fmt, va_list args)
{
	char fmtbuf[MSGBUFSIZE];
	char msgbuf[MSGBUFSIZE];
	char *txt = NULL;
	int priority = LOG_INFO;

	if (level > log_level)
		return;

	switch (level) {
		case SYSLOG_LEVEL_ERROR:
			txt = "ERROR";
			priority = LOG_ERR;
			break;
		case SYSLOG_LEVEL_INFO:
			txt = "INFO";
			priority = LOG_INFO;
			break;
		case SYSLOG_LEVEL_DEBUG:
			txt = "DEBUG";
			priority = LOG_DEBUG;
			break;
		default:
			priority = LOG_ERR;
			break;
	}

	if (txt != NULL ) {
		snprintf(fmtbuf, sizeof(fmtbuf), "%s: %s", txt, fmt);
		vsnprintf(msgbuf, sizeof(msgbuf), fmtbuf, args);
	} else {
		vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
	}

	if (log_is_on_stderr())
		fprintf(stderr, "%.500s", msgbuf);
	else {
		openlog("tierd", LOG_PID, LOG_DAEMON);
		syslog(priority, "%.500s", msgbuf);
		closelog();
	}
}

void tierdlog(tierd_loglevel level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	tierdlogv(level, fmt, args);
	va_end(args);
}
