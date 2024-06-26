#include "IntelMonitor.hpp"

using namespace std;

int ArchMonitor::init(int interval_in_us) 
{ 	
	return pcm_monitor_init(interval_in_us);
}

void ArchMonitor::start() 
{ 
	pcm_monitor_start();
}

void ArchMonitor::stop() 
{ 
	pcm_monitor_stop();
}

void ArchMonitor::report_start() 
{ 
	pcm_monitor_report_start();
}

int ArchMonitor::get_BW(int socketId, int cxlPortId, 
		float *readBw, float *writeBw) 
{ 
	return pcm_monitor_get_BW(socketId, cxlPortId, readBw, writeBw);
}

void ArchMonitor::report_finish() 
{
	pcm_monitor_report_finish();
}

void ArchMonitor::updateBandwidthHistory(int nodeId, 
		float readBw, float writeBw)
{
	unsigned int bwType;
	vector<float> bwList = { readBw, writeBw, readBw + writeBw };

	for (bwType = 0; bwType < bandwidthTypeNumber; bwType++) {
		float currentBw = bwList[bwType];
		float currentMax = maxBandwidthInfo[nodeId][bwType];
		bandwidthHistory[historyHead][nodeId][bwType] = currentBw;
		if (currentBw > currentMax) {
			maxBandwidthInfo[nodeId][bwType] = currentBw;
		}
	}
}

void ArchMonitor::updateCapacityHistory(int nodeId)
{
	long long freeSize;
	long long totalSize;

	totalSize = numa_node_size64(nodeId, &freeSize);
	if (totalSize == -1L) {
		maxCapacityInfo[nodeId] = 0;
		return;
	}

	maxCapacityInfo[nodeId] = totalSize;
	capacityHistory[historyHead][nodeId] = freeSize;
}

void ArchMonitor::updateNodeSaturation()
{
	nodeSaturationAgent->update(*this);
}

void ArchMonitor::updateMaxBandwidth()
{
	maxBandwidthAgent->update(*this);
}

void ArchMonitor::updateRemainBandwidth()
{
	remainBandwidthAgent->update(*this);
}

void ArchMonitor::updateFallbackOrder()
{
	fallbackOrderAgent->update(*this);
}

void ArchMonitor::updateHistoryHead()
{
	historyHead++;
	if (historyHead >= maxHistorySize)
		historyHead = 0;
}

ArchMonitor::ArchMonitor(AgentFactory *_agentFactory, InputParser *_inputParser,
		int _systemSocketNumber, int _systemNodeNumber)
{
	cout << "Monitor Constructor" << endl;

	bandwidthHistory.clear();
	capacityHistory.clear();
	maxBandwidthInfo.clear();
	maxCapacityInfo.clear();

	agentFactory = _agentFactory;
	inputParser = _inputParser;
	systemSocketNumber = _systemSocketNumber;
	systemNodeNumber = _systemNodeNumber;

	bandwidthHistory.resize(maxHistorySize);
	for(unsigned int i=0;i<maxHistorySize;i++) {
		bandwidthHistory[i].resize(systemNodeNumber);
		for (int j=0;j<systemNodeNumber;j++) {
			bandwidthHistory[i][j].resize(bandwidthTypeNumber);
		}
	}
	capacityHistory.resize(maxHistorySize);
	for(unsigned int i=0;i<maxHistorySize;i++) {
		capacityHistory[i].resize(systemNodeNumber);
	}
	maxBandwidthInfo.resize(systemNodeNumber);
	for (int i=0;i<systemNodeNumber;i++) {
		maxBandwidthInfo[i].resize(bandwidthTypeNumber);
	}
	maxCapacityInfo.resize(systemNodeNumber);

	stopFlag.store(false);
	workerThread = nullptr;

	historyHead = 0;

	nodeSaturationAgent = _agentFactory->createAgent(0);
	maxBandwidthAgent = _agentFactory->createAgent(1);
	remainBandwidthAgent = _agentFactory->createAgent(2);
	fallbackOrderAgent = _agentFactory->createAgent(3);
}

void ArchMonitor::print(int nodeId, float readBw, float writeBw)
{
	auto now = std::chrono::system_clock::now();
	time_t now_time = std::chrono::system_clock::to_time_t(now);
	std::tm *local_time = std::localtime(&now_time);

	cout << "[" << std::put_time(local_time, "%H:%M:%S") << "]";
	cout << " " << "[" << nodeId << "]" << " ";
	cout << readBw << " " << writeBw << " " << readBw + writeBw << endl;
}

void ArchMonitor::monitorLoop()
{
	int nodeId;

	if (init(inputParser->getInterval())) {
		cerr << "Monitor Init Failed." << endl;
		return;
	}

	start();
	while (1) {
		if (stopFlag.load())
			break;

		report_start();
		for (nodeId = 0; nodeId < systemNodeNumber; nodeId++) {			
			float readBw = 0.0f;
			float writeBw = 0.0f;

			int cxlPortId = 0;
			int temp = nodeId;
			if (nodeId < systemSocketNumber) {
				cxlPortId = -1;
			} else {
				temp = 0;
			}

			if (get_BW(temp, cxlPortId, &readBw, &writeBw)) {
				cerr << "Monitor Error: get_BW" << endl;
				break;
			}

			if (readBw > SPIKE || isnan(readBw) 
				|| writeBw > SPIKE || isnan(writeBw)) {
				readBw = 0.0;
				writeBw = 0.0;
			}

			print(nodeId, readBw, writeBw);
			updateBandwidthHistory(nodeId, readBw, writeBw);
			updateCapacityHistory(nodeId);
		}
		report_finish();

		updateNodeSaturation();
		updateMaxBandwidth();
		updateRemainBandwidth();

		updateHistoryHead();
	}
	stop();	
}

int ArchMonitor::launch()
{
	cout << "Launch Monitor" << endl;

	workerThread = make_unique<thread>(&ArchMonitor::monitorLoop, this);

	return 0;
}

int ArchMonitor::terminate()
{
	stopFlag.store(true);
	if (workerThread && workerThread->joinable()) {
		workerThread->join();
	}

	cout << "Terminate Monitor" << endl;

	return 0;
}

void ArchMonitor::notifyFromSignalHandlerStop()
{
	terminate();
}

void ArchMonitor::notifyFromSignalHandlerChange()
{
	SystemInfo systemInfo;
	systemSocketNumber = systemInfo.getSystemSocketNumber();
	systemNodeNumber = systemInfo.getSystemNodeNumber();

	historyHead = 0;

	bandwidthHistory.clear();
	capacityHistory.clear();
	maxBandwidthInfo.clear();
	maxCapacityInfo.clear();

	bandwidthHistory.resize(maxHistorySize);
	for(unsigned int i=0;i<maxHistorySize;i++) {
		bandwidthHistory[i].resize(systemNodeNumber);
		for (int j=0;j<systemNodeNumber;j++) {
			bandwidthHistory[i][j].resize(bandwidthTypeNumber);
		}
	}
	capacityHistory.resize(maxHistorySize);
	for(unsigned int i=0;i<maxHistorySize;i++) {
		capacityHistory[i].resize(systemNodeNumber);
	}
	maxBandwidthInfo.resize(systemNodeNumber);
	for (int i=0;i<systemNodeNumber;i++) {
		maxBandwidthInfo[i].resize(bandwidthTypeNumber);
	}
	maxCapacityInfo.resize(systemNodeNumber);
}

void ArchMonitor::notifyFromBandwidthLoader() 
{
	updateFallbackOrder();
}
