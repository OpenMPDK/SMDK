#ifndef FALLBACK_ORDER_INFO_H
#define FALLBACK_ORDER_INFO_H

#include "AgentInterface.hpp"

using namespace std;

class ArchMonitor;

class FallbackOrderAgent : public AgentInterface
{
	private:
		const string FALLBACK_ORDER_BW_FILE = "/fallback_order_bandwidth";
		const string FALLBACK_ORDER_TIER_FILE = "/fallback_order_tier";
		const int managedFileNumber = 2;
		vector<vector<string>> managedFileList;

		int systemNodeNumber;
		int systemSocketNumber;
		vector<string> directoryPath;
		vector<vector<float>> maxBandwidthInfo;

		struct NodeInfo {
			int nodeId;
			int bandwidth;
		};
		struct Cluster {
			int nodeNumber;
			NodeInfo *nodeList;
		};
		float TOLERANCE = 5.0f;

	public:
		FallbackOrderAgent(
				int _systemSocketNumber, int _systemNodeNumber,
				vector<string> _directoryPath);

		static int compare(const void *_a, const void *_b);
		int updateFallbackOrderByTier();
		void setCluster(Cluster *cluster, int idx, int nid);
		void nodeClustering(vector<NodeInfo> &sorted, 
				vector<vector<NodeInfo>> &nodeInfo);
		void updateFallbackOrderByBandwidth();
		int write(vector<NodeInfo> list, int sid, int start, int end, int idx);

		int createManagedFiles() override;
		void update(const ArchMonitor& monitorWorker) override;
};

#endif
