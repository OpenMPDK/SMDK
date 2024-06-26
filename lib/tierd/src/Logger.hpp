#include <iostream>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <string>
#include <syslog.h>

using namespace std;

enum LogLevel {
	ERROR,
	INFO,
	DEBUG
};

class Logger
{
	private:
		const static int LOG_BUF_SIZE = 1024;

		bool daemonize;

		LogLevel logLevel;
		
		void doLog(LogLevel _logLevel, const char *fmt, va_list args);

	public:
		Logger(LogLevel _logLevel, bool _daemonize);
		void log(LogLevel _logLevel, const char *fmt, ...);

	protected:
};
