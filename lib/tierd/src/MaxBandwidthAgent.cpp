#include "MaxBandwidthAgent.hpp"
#if defined(ARCH_INTEL)
#include "IntelMonitor.hpp"
#elif defined(ARCH_AMD)
#include "AMDMonitor.hpp"
#endif

using namespace std;

MaxBandwidthAgent::MaxBandwidthAgent(
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
				+ MAX_RD_BW_FILE);
		managedFileList[i].push_back(directoryPath[i] 
				+ MAX_WR_BW_FILE);
		managedFileList[i].push_back(directoryPath[i] 
				+ MAX_RW_BW_FILE);
	}

	writtenMaxBandwidth.resize(systemNodeNumber);
	for (int i=0;i<systemNodeNumber;i++) {
		writtenMaxBandwidth[i].resize(managedFileNumber);
	}

	for (int i=0;i<systemNodeNumber;i++) {
		for (int j=0;j<managedFileNumber;j++) {
			writtenMaxBandwidth[i][j] = 0.0f;
		}
	}
}

int MaxBandwidthAgent::createManagedFiles()
{
	cout << "MaxBandwidthAgent createFile called..." << endl;

	int i, j;

	for (i = 0; i < systemNodeNumber; i++) {
		for (j = 0; j < managedFileNumber; j++) {
			FILE *fp = fopen(managedFileList[i][j].c_str(),
					"w");
			if (fp == NULL) {
				return -1;
			}

			fprintf(fp, "-1.0");

			fclose(fp);
		}
	}

	return 0;
}

int MaxBandwidthAgent::write(int nodeId, unsigned int bwType, float value)
{
	FILE *fp = fopen(managedFileList[nodeId][bwType].c_str(), "w");
	if (fp == NULL)
		return -1;

	fprintf(fp, "%.2f\n", value);

	fclose(fp);

	return 0;
}

void MaxBandwidthAgent::update(const ArchMonitor& monitorWorker)
{
	int nodeId;
	int systemNodeNumber;
	unsigned int bwType;
	unsigned int bandwidthTypeNumber;

	systemNodeNumber = monitorWorker.systemNodeNumber;
	bandwidthTypeNumber = monitorWorker.bandwidthTypeNumber;

	for (nodeId = 0; nodeId < systemNodeNumber; nodeId++) {
		for (bwType = 0; bwType < bandwidthTypeNumber; bwType++) {
			float writtenMax = writtenMaxBandwidth[nodeId][bwType];
			float currentMax = monitorWorker.maxBandwidthInfo[nodeId][bwType];
			if (currentMax > writtenMax) {
				write(nodeId, bwType, currentMax);
				writtenMaxBandwidth[nodeId][bwType] = currentMax;
			}
		}
	}
}
