#include <fcntl.h>
#include <filesystem>
#include <sys/ioctl.h>

#include "Logger.hpp"
#include "SystemInfo.hpp"
#include "InputParser.hpp"
#include "AgentFactory.hpp"
#include "SignalHandler.hpp"
#include "BandwidthLoader.hpp"
#include "MonitorInterface.hpp"
#if defined(ARCH_INTEL)
#include "IntelMonitor.hpp"
#elif defined(ARCH_AMD)
#include "AMDMonitor.hpp"
#endif

namespace fs = std::filesystem;

SignalHandler* SignalHandler::instance = nullptr;

class Tierd 
{
	private:
		const bool INVALID = false;
		const bool VALID = true;
		const string TIERD_DEV_PATH = "/dev/tierd";
		const string TIERD_RUN_PATH = "/run/tierd";
		vector<string> nodeDirectoryPath;

		int systemSocketNumber;
		int systemNodeNumber;

		bool registerTierdToDriver()
		{
			int fd, ret;

			fd = open(TIERD_DEV_PATH.c_str(), O_RDWR);
			if (fd < 0) {
				cerr << "Failed to open device: " << strerror(errno) << endl;
				return INVALID;
			}

			ret = ioctl(fd, _IO('M', 0));
			if (ret < 0) {
				cerr << "Failed to ioctl: " << strerror(errno) << endl;
				close(fd);
				return INVALID;
			}

			close(fd);

			return VALID;
		}

		MonitorInterface *getMonitor(AgentFactory *agentFactory, 
				InputParser *inputParser)
		{
#if defined(ARCH_INTEL) || defined(ARCH_AMD)
			return new ArchMonitor(agentFactory, inputParser,
					systemSocketNumber, systemNodeNumber);
#else
			return nullptr;
#endif
		}

		int createDirectory(string path)
		{
			if (fs::exists(path)) {
				cout << path << " already exists. Using it." << endl;
				return 0;
			}

			if (fs::create_directory(path)) {
				cout << "Create " << path << " success." << endl;
				return 0;
			}

			cerr << "Failed to create " << path << endl;
			return -1;
		}

		int createTierdNodeDirectory(int nodeId)
		{
			string nodeDirPath = TIERD_RUN_PATH + "/node" + to_string(nodeId);
			if (createDirectory(nodeDirPath)) {
				return -1;
			}

			nodeDirectoryPath.push_back(nodeDirPath);
			return 0;
		}

		int createTierdRootDirectory()
		{
			return createDirectory(TIERD_RUN_PATH);
		}

		int createTierdDirectory()
		{
			if (createTierdRootDirectory())
				return -1;

			for (int nodeId = 0; nodeId < systemNodeNumber; nodeId++) {
				if (createTierdNodeDirectory(nodeId))
					return -1;
			}

			return 0;
		}

	public:
		Tierd()
		{
			SystemInfo systemInfo;
			systemNodeNumber = systemInfo.getSystemNodeNumber();
			systemSocketNumber = systemInfo.getSystemSocketNumber();
		}

		int run(int argc, char *argv[])
		{
			// Parsing Input. e.g. window, interval, config_file
			InputParser inputParser(argc, argv);
			if (!inputParser.isValid()) {
				cout << "Failed to parse input" << endl;
				return 1;
			}

			// Register to kernel driver for getting memory on/offline signal.
			if (!registerTierdToDriver()) {
				cout << "Failed to register tierd to driver" << endl;
				return 1;
			}

			// Intercept SIGUSR1, SIGUSR2, SIGINT signal.
			SignalHandler* signalHandler = SignalHandler::getInstance();
			signalHandler->initSignalHandler();

			if (createTierdDirectory()) {
				cout << "Failed to create tierd directory" << endl;
				return 1;
			}

			// File I/O Agent.
			AgentFactory agentFactory(systemSocketNumber, systemNodeNumber, 
					nodeDirectoryPath);

			// Set arch dependent PMU monitoring module.
			MonitorInterface *monitorWorker = 
				getMonitor(&agentFactory, &inputParser);
			if (monitorWorker == nullptr) {
				cout << "Please check cpu vendor id" << endl;
				return 1;
			}

			// Create bandwidth loader which operates forked mlc
			BandwidthLoader *bandwidthLoader = new BandwidthLoader(&inputParser);

			// Register notifier before launch
			signalHandler->addObserver(monitorWorker);
			signalHandler->addObserver(bandwidthLoader); 
			bandwidthLoader->addObserver(monitorWorker); 

			// Launch Monitor as Background Thread
			if (monitorWorker->launch()) {
				cout << "Fail to launch monitorWorker" << endl;
				return 1;
			}

			// Launch Bandwidth Load Workload and Wait
			if (bandwidthLoader->launch()) {
				cout << "Fail to launch bandwidthLoader" << endl;
				return 1;
			}

			monitorWorker->terminate();

			cout << "Program Finish..." << endl;

			return 0;
		}

	protected:
};

int main(int argc, char* argv[])
{
	if (getuid() != 0) {
		cout << "Tierd should be running with sudo privileges." << endl;
		return 0;
	}

	Tierd tierd;
	if (tierd.run(argc, argv)) {
		cout << "Failed to run tierd." << endl;
		return 1;
	}

	return 0;
}
