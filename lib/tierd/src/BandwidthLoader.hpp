#ifndef BANDWIDTH_LOADER_H
#define BANDWIDTH_LOADER_H

#include <vector>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "Worker.hpp"
#include "Observer.hpp"
#include "InputParser.hpp"

using namespace std;

#define GETTIME(begin, end) \
	((end.tv_sec - begin.tv_sec) + \
	 (end.tv_nsec - begin.tv_nsec) / 1000000000.0)

class BandwidthLoader: public Worker, public Observer
{
	private:
		atomic<bool> killFlag;
		atomic<bool> stopFlag;
		atomic<bool> signalReceived;

		InputParser *inputParser;

		vector<Observer *> observers;
		unique_ptr<thread> workerThread;
	    std::mutex mtx;
    	std::condition_variable cv;

	public:
		BandwidthLoader(InputParser *_inputParser);
		int launchProcess();
		int launch() override;
		int terminate() override;
		void notifyFromSignalHandlerStop() override;
		void notifyFromSignalHandlerChange() override;
		void notifyFromBandwidthLoader() override;
		void addObserver(Observer *observer);
		void notifyObservers();
		void loaderLoop();

	protected:
};

#endif
