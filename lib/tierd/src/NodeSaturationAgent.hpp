#ifndef NODE_SATURATION_STATUS_H
#define NODE_SATURATION_STATUS_H

#include "AgentInterface.hpp"

using namespace std;

class ArchMonitor;

class NodeSaturationAgent : public AgentInterface
{
	private:		
		const string IDLE_STATE_BW_FILE = "/idle_state_bandwidth";
		const string IDLE_STATE_CAPA_FILE = "/idle_state_capacity";
		const int managedFileNumber = 2;
		const ArchMonitor *monitorWorker;

		vector<vector<string>> managedFileList;
		int systemNodeNumber;
		int systemSocketNumber;
		vector<string> directoryPath;

		const float bandwidthThreshold = 90.0f;
		const float capacityThreshold = 90.0f;

		bool checkNodeSaturation();

	public:
		NodeSaturationAgent(
				int _systemSocketNumber, int _systemNodeNumber,
				vector<string> _directoryPath);

		int createManagedFiles() override;
		int write(vector<vector<bool>>& val);
		void update(const ArchMonitor& monitorWorker) override;

	protected:
};

#endif
