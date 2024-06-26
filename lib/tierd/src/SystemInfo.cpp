#include "SystemInfo.hpp"

void SystemInfo::setSystemSocketNumber()
{
	int socketNum;
	FILE *fp;
	char buffer[128];

	fp = popen("lscpu | grep 'Socket(s):'", "r");
	if (fp == NULL) {
		systemSocketNumber = 0;
		return;
	}

	if (fgets(buffer, sizeof(buffer), fp) != NULL) {
		socketNum = buffer[strlen(buffer) - 2] - '0';
	} else {
		socketNum = 0;
	}

	pclose(fp);

	systemSocketNumber = socketNum;
	if (systemSocketNumber <= 0)
		systemSocketNumber = 0;
}

void SystemInfo::setSystemNodeNumber()
{
	systemNodeNumber = numa_max_node() + 1;
	if (systemNodeNumber <= 0)
		systemNodeNumber = 0;
}

SystemInfo::SystemInfo()
{
	setSystemNodeNumber();
	setSystemSocketNumber();
}

int SystemInfo::getSystemNodeNumber()
{
	return systemNodeNumber;
}

int SystemInfo::getSystemSocketNumber()
{
	return systemSocketNumber;
}
