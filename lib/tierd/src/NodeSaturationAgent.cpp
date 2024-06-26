#include "NodeSaturationAgent.hpp"
#if defined(ARCH_INTEL)
#include "IntelMonitor.hpp"
#elif defined(ARCH_AMD)
#include "AMDMonitor.hpp"
#endif

using namespace std;

NodeSaturationAgent::NodeSaturationAgent(
		int _systemSocketNumber, int _systemNodeNumber,
		vector<string> _directoryPath)
{
	int i;
	systemSocketNumber = _systemSocketNumber;
	systemNodeNumber = _systemNodeNumber;
	directoryPath = _directoryPath;
	managedFileList.resize(systemNodeNumber);	
	for (i = 0; i < systemNodeNumber; i++) {
		managedFileList[i].push_back(directoryPath[i]
				+ IDLE_STATE_BW_FILE);
		managedFileList[i].push_back(directoryPath[i]
				+ IDLE_STATE_CAPA_FILE);
	}
}

int NodeSaturationAgent::createManagedFiles()
{
	cout << "NodeSaturationAgentHandler createFile called..." << endl;

	int i, j;

	for (i = 0; i < systemNodeNumber; i++) {
		for (j = 0; j < managedFileNumber; j++) {
			FILE *fp = fopen(managedFileList[i][j].c_str(), "w");
			if (fp == NULL) {
				return -1;
			}

			fprintf(fp, "true\n");

			fclose(fp);
		}
	}

	return 0;
}

int NodeSaturationAgent::write(vector<vector<bool>>& val)
{
	int i, j;

	for (i = 0; i < systemNodeNumber; i++) {
		for (j = 0; j < managedFileNumber; j++) {
			FILE *fp = fopen(managedFileList[i][j].c_str(), "w");
			if (fp == NULL)
				return -1;

			fprintf(fp, !val[i][j] ? "true\n" : "false\n");

			fclose(fp);
		}
	}

	return 0;
}

void NodeSaturationAgent::update(const ArchMonitor &_monitorWorker)
{
	monitorWorker = &_monitorWorker;

	const int bandwidthTypeNumber = monitorWorker->bandwidthTypeNumber;
	const int maxHistorySize = monitorWorker->maxHistorySize;
	const int windowSize = monitorWorker->inputParser->getWindow();
	const int start = monitorWorker->historyHead;
	const int systemNodeNumber = monitorWorker->systemNodeNumber;

	int end = start - windowSize; 

	vector<vector<bool>> saturationStatus;
	saturationStatus.resize(systemNodeNumber);
	for (int nodeId = 0; nodeId < systemNodeNumber; nodeId++) {
		vector<float> avgBandwidth(bandwidthTypeNumber, 0.0f);
		float avgCapacity = 0.0f;
		bool bandwidthSaturated = false;
		bool capacitySaturated = false;

		// Accumulate bandwidth & capacity history during window.
		for (int i = start; i > end; i--) {
			int history = (i + maxHistorySize) % maxHistorySize;
			for (int bwType = 0; bwType < bandwidthTypeNumber; bwType++) {
				avgBandwidth[bwType] += 
					monitorWorker->bandwidthHistory[history][nodeId][bwType];
			}
			avgCapacity += monitorWorker->capacityHistory[history][nodeId];
		}

		// Get average.
		for (int bwType = 0; bwType < bandwidthTypeNumber; bwType++) {
			avgBandwidth[bwType] /= windowSize;
		}
		avgCapacity /= windowSize;

		// Check the saturation status.
		for (int bwType = 0; bwType < bandwidthTypeNumber; bwType++) {
			float limit = monitorWorker->maxBandwidthInfo[nodeId][bwType];
			limit *= bandwidthThreshold;
			limit /= 100.0f;
			if (avgBandwidth[bwType] > limit) {
				bandwidthSaturated = true;
				break;
			}
		}
		float limit = monitorWorker->maxCapacityInfo[nodeId];
		limit *= capacityThreshold;
		limit /= 100.0f;
		if (avgCapacity > limit)
			capacitySaturated = true;
		
		// Push back for write to file
		saturationStatus[nodeId].push_back(bandwidthSaturated);
		saturationStatus[nodeId].push_back(capacitySaturated);
	}	
	write(saturationStatus);
}
