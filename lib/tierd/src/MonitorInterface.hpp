#ifndef MONITOR_INTERFACE_H
#define MONITOR_INTERFACE_H

#include <cmath>
#include <ctime>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
#include <chrono>
#include <iomanip>
#include <iostream>

#include "Worker.hpp"
#include "Observer.hpp"
#include "AgentFactory.hpp"
#include "AgentInterface.hpp"

using namespace std;

class MonitorInterface : public Worker, public Observer
{
	private:
	public:
		virtual int launch() override = 0;
		virtual int terminate() override = 0;
		virtual void notifyFromSignalHandlerStop() override = 0;
		virtual void notifyFromSignalHandlerChange() override = 0;
		virtual void notifyFromBandwidthLoader() override = 0;
	protected:
		virtual int init(int interval) = 0;
		virtual void start() = 0;
		virtual int get_BW(int socketId, int cxlPortId, 
				float *readBw, float *writeBw) = 0;
		virtual void stop() = 0;
};
#endif
