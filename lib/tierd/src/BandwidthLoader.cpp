#include "BandwidthLoader.hpp"

BandwidthLoader::BandwidthLoader(InputParser *_inputParser)
{
	killFlag.store(false);
	stopFlag.store(false);
	signalReceived.store(false);

	inputParser = _inputParser;
}

int BandwidthLoader::launch()
{
	launchProcess();
	while (!stopFlag.load()) {
		{
			std::unique_lock<std::mutex> lock(mtx);
			cv.wait(lock, [this]{ return signalReceived.load(); });
			signalReceived.store(false);
		}

		if (killFlag.load()) {
			killFlag.store(false);
			launchProcess();
		}
	}

	return 0;
}

int BandwidthLoader::launchProcess()
{
	pid_t pid = fork();

	if (pid < 0) {
		cout << "Fork Fail" << endl;
		return -1;
	} 

	if (pid == 0) {
		string mlcPath = inputParser->getMlcPath();
		string mlcFileName = inputParser->getMlcFileName();
		cout << "Launch Bandwidth Loader Workload.. " << mlcPath << endl;
		close(STDOUT_FILENO);
		execl(mlcPath.c_str(), mlcFileName.c_str(), "--bandwidth_matrix", NULL);
	} else {
		int status;
		struct timespec begin, end;
		clock_gettime(CLOCK_MONOTONIC, &begin);
		while (1) {
			if (killFlag.load()) {
				kill(pid, SIGKILL);
				wait(nullptr);
			}

			if (stopFlag.load()) {
				kill(pid, SIGKILL);
				wait(nullptr);
			}

			pid_t result = waitpid(pid, &status, WNOHANG);

			if (result == -1) {
				cout << "Child Process Killed..." << endl;
				notifyObservers();
				break;
			}

			if (result == 0) {
				clock_gettime(CLOCK_MONOTONIC, &end);
			} else {
				cout << "Bandwidth Loader Workload Finish (" << 
						GETTIME(begin, end) << "s)" << endl;
				cout << "Notify..." << endl;
				notifyObservers();
				break;
			}
		}
	}

	return 0;
}

int BandwidthLoader::terminate()
{
	return 0;
}

void BandwidthLoader::notifyFromSignalHandlerStop()
{
	cout << "Get Stop Signal from SignalHandler in BWLoader" << endl;
	std::lock_guard<std::mutex> lock(mtx);
	signalReceived.store(true);
	stopFlag.store(true);
	cv.notify_one();
}

void BandwidthLoader::notifyFromSignalHandlerChange()
{
	cout << "Get Change Signal from SignalHandler in BWLoader" << endl;
	std::lock_guard<std::mutex> lock(mtx);
	signalReceived.store(true);
	killFlag.store(true);
	cv.notify_one();
}

void BandwidthLoader::notifyFromBandwidthLoader() { }

void BandwidthLoader::addObserver(Observer *observer)
{
	observers.push_back(observer);
}

void BandwidthLoader::notifyObservers()
{
	for (auto observer : observers) {
		observer->notifyFromBandwidthLoader();
	}
}

