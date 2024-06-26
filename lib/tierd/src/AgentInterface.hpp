#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <numa.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <iostream>

using namespace std;

class ArchMonitor;

class AgentInterface
{
	private:
	public:
		virtual int createManagedFiles() = 0;
		virtual void update(const ArchMonitor& monitorWorker) = 0;

	protected:
};

#endif
