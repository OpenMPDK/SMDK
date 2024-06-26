#include "RemainBandwidthAgent.hpp"
#if defined(ARCH_INTEL)
#include "IntelMonitor.hpp"
#elif defined(ARCH_AMD)
#include "AMDMonitor.hpp"
#endif

using namespace std;

RemainBandwidthAgent::RemainBandwidthAgent(
		int _systemSocketNumber, int _systemNodeNumber,
		vector<string> _directoryPath)
{
	int i;
	systemSocketNumber = _systemSocketNumber;
	systemNodeNumber = _systemNodeNumber;
	directoryPath = _directoryPath;
	managedFileLists.resize(systemNodeNumber);
	for (i = 0; i < systemNodeNumber; i++) {
		managedFileLists[i].push_back(directoryPath[i] + AVAILABLE_RD_BW_FILE);
		managedFileLists[i].push_back(directoryPath[i] + AVAILABLE_WR_BW_FILE);
		managedFileLists[i].push_back(directoryPath[i] + AVAILABLE_BW_FILE);
	}

	writtenRemainBandwidth.resize(systemNodeNumber);
	for (int i=0;i<systemNodeNumber;i++) {
		writtenRemainBandwidth[i].resize(managedFileNumber);
	}

	for (int i=0;i<systemNodeNumber;i++) {
		for (int j=0;j<managedFileNumber;j++) {
			writtenRemainBandwidth[i][j] = 0.0;
		}
	}

}

int RemainBandwidthAgent::createManagedFiles()
{
	cout << "Remain createFile called..." << endl;

	int i, j;

	for (i = 0; i < systemNodeNumber; i++) {
		for (j = 0; j < managedFileNumber; j++) {
			FILE *fp = fopen(managedFileLists[i][j].c_str(), "w");
			if (fp == NULL)
				return -1;

			fprintf(fp, "-1.0");

			fclose(fp);
		}
	}

	return 0;
}

int RemainBandwidthAgent::write(int nodeId, unsigned int bwType, float value)
{
	FILE *fp = fopen(managedFileLists[nodeId][bwType].c_str(), "w");
	if (fp == NULL)
		return -1;

	fprintf(fp, "%.2f\n", value);

	fclose(fp);

	return 0;
}

void RemainBandwidthAgent::update(const ArchMonitor& monitorWorker) 
{
	int nodeId;
	int systemNodeNumber;
	int historyHead;
	unsigned int bwType;
	unsigned int bandwidthTypeNumber;

	historyHead = monitorWorker.historyHead;
	systemNodeNumber = monitorWorker.systemNodeNumber;
	bandwidthTypeNumber = monitorWorker.bandwidthTypeNumber;

	for (nodeId = 0; nodeId < systemNodeNumber; nodeId++) {
		for (bwType = 0; bwType < bandwidthTypeNumber; bwType++) {
			float curMax, curBw, curRemain, writtenRemain;

			curMax = monitorWorker.maxBandwidthInfo[nodeId][bwType];
			curBw = monitorWorker.bandwidthHistory[historyHead][nodeId][bwType];

			curRemain = curMax - curBw;
			writtenRemain = writtenRemainBandwidth[nodeId][bwType];
			if (abs(curRemain - writtenRemain) > TOLERANCE) {
				write(nodeId, bwType, curRemain);
				writtenRemainBandwidth[nodeId][bwType] = curRemain;
			}
		}
	}
}
