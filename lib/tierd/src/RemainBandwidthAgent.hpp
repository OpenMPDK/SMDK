#ifndef AVAILABLE_BANDWIDTH_STATUS_H
#define AVAILABLE_BANDWIDTH_STATUS_H

#include "AgentInterface.hpp"

class ArchMonitor;

class RemainBandwidthAgent : public AgentInterface
{
	private:
		const string AVAILABLE_RD_BW_FILE = "/remain_read_bandwidth";
		const string AVAILABLE_WR_BW_FILE = "/remain_write_bandwidth";
		const string AVAILABLE_BW_FILE = "/remain_bandwidth";
		const int managedFileNumber = 3;
		vector<vector<string>> managedFileLists;

		vector<string> directoryPath;
		int systemNodeNumber;
		int systemSocketNumber;
		vector<vector<float>> writtenRemainBandwidth;
		// Update whenever there is a difference of 1000 MB/s.
		const float TOLERANCE = 1000.0f;
		int write(int nodeId, unsigned int bwType, float value);

	public:
		RemainBandwidthAgent(
				int _systemSocketNumber, int _systemNodeNumber,
				vector<string> _directoryPath);
		int createManagedFiles() override;
		void update(const ArchMonitor& monitorWorker) override;
};

#endif
