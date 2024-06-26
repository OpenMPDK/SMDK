#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

#include <cstdio>
#include <cstring>
#include <numa.h>

class SystemInfo
{
	private:
		int systemSocketNumber;
		int systemNodeNumber;

		void setSystemSocketNumber();
		void setSystemNodeNumber();

	public:
		SystemInfo();
		int getSystemNodeNumber();
		int getSystemSocketNumber();

	protected:
};

#endif
