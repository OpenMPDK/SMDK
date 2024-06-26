#include "Logger.hpp"

Logger::Logger(LogLevel _logLevel, bool _daemonize)
{
	logLevel = _logLevel;
	daemonize = _daemonize;
}

void Logger::doLog(LogLevel level, const char *fmt, va_list args)
{
	char *logHeader = NULL;
	char formatBuffer[LOG_BUF_SIZE];
	char messageBuffer[LOG_BUF_SIZE];

	int logPriority = LOG_INFO;

	if (level > logLevel) {
		return;
	}

	switch(level) {
		case LogLevel::ERROR:
			logHeader = (char *)"ERROR";
			logPriority = LOG_ERR;
			break;
		case LogLevel::INFO:
			logHeader = (char *)"INFO";
			logPriority = LOG_INFO;
			break;
		case LogLevel::DEBUG:
			logHeader = (char *)"DEBUG";
			logPriority = LOG_DEBUG;
			break;
		default:
			logHeader = (char *)"ERROR";
			logPriority = LOG_ERR;
			break;
	}

	if (daemonize) {
		vsnprintf(messageBuffer, sizeof(messageBuffer), fmt, args);

		openlog("tierd", LOG_PID, LOG_DAEMON);
		syslog(logPriority, "%.500s", messageBuffer);
		closelog();
	} else {
		snprintf(formatBuffer, sizeof(formatBuffer), "%s: %s", logHeader, fmt);
		vsnprintf(messageBuffer, sizeof(messageBuffer), formatBuffer, args);

		fprintf(stderr, "%.500s", messageBuffer);
	}
}

void Logger::log(LogLevel level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	doLog(level, fmt, args);
	va_end(args);
}
