#ifndef MAX_BANDWIDTH_INFO_HANDLER
#define MAX_BANDWIDTH_INFO_HANDLER

#include "AgentInterface.hpp"

using namespace std;

class MaxBandwidthAgent : public AgentInterface
{
	private:
		const string MAX_RD_BW_FILE = "/max_read_bw";
		const string MAX_WR_BW_FILE = "/max_write_bw";
		const string MAX_RW_BW_FILE = "/max_bw";
		const int managedFileNumber = 3;
		vector<vector<string>> managedFileList;

		int systemNodeNumber;
		int systemSocketNumber;
		vector<vector<float>> writtenMaxBandwidth;
		vector<string> directoryPath;

	public:
		MaxBandwidthAgent(
				int _systemSocketNumber, int _systemNodeNumber, 
				vector<string> _directoryPath);

		int createManagedFiles() override;
		int write(int nodeId, unsigned int bwType, float value);
		void update(const ArchMonitor& monitorWorker) override;

	protected:
};

#endif
