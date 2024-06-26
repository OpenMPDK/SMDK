#include "FallbackOrderAgent.hpp"
#if defined(ARCH_INTEL)
#include "IntelMonitor.hpp"
#elif defined(ARCH_AMD)
#include "AMDMonitor.hpp"
#endif

using namespace std;

FallbackOrderAgent::FallbackOrderAgent(
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
				+ FALLBACK_ORDER_BW_FILE);
		managedFileList[i].push_back(directoryPath[i]
				+ FALLBACK_ORDER_TIER_FILE);
	}
}

int FallbackOrderAgent::createManagedFiles()
{
	cout << "FallbackOrderAgent createFile called..." << endl;

	int i, j;

	for (i = 0; i < systemSocketNumber; i++) {
		for (j = 0; j < managedFileNumber; j++) {
			FILE *fp = fopen(managedFileList[i][j].c_str(), "w");
			if (fp == NULL) {
				return -1;
			}

			fprintf(fp, "-1\n");

			fclose(fp);
		}
	}

	return 0;
}

int FallbackOrderAgent::compare(const void *_a, const void *_b)
{
	NodeInfo *a = (NodeInfo *)_a;
	NodeInfo *b = (NodeInfo *)_b;
	if (a->bandwidth == b->bandwidth)
		return 0;
	else if (a->bandwidth > b->bandwidth)
		return 1;
	return -1;
}

int FallbackOrderAgent::updateFallbackOrderByTier()
{
	int nodeId;

	vector<vector<NodeInfo>> nodeInfo(systemSocketNumber);

	for (int i = 0; i < systemSocketNumber; i++)
		nodeInfo[i].resize(systemNodeNumber);

	for (int from = 0; from < systemSocketNumber; from++) {
		for (int to = 0; to < systemNodeNumber; to++) {
			nodeInfo[from][to].bandwidth = numa_distance(from, to);
			nodeInfo[from][to].nodeId = to;
		}
		qsort(&nodeInfo[from], systemSocketNumber, sizeof(NodeInfo), compare);
		qsort(&nodeInfo[from] + systemSocketNumber, 
				systemNodeNumber - systemSocketNumber,
				sizeof(NodeInfo), compare);
	}

	for (nodeId = 0; nodeId < systemSocketNumber; nodeId++) {
		write(nodeInfo[nodeId], nodeId, 0, systemNodeNumber, 1);
	}

	return 0;
}

void FallbackOrderAgent::setCluster(Cluster *cluster, int idx, int nid)
{
	cluster[idx].nodeList[cluster[idx].nodeNumber].nodeId = nid;
	cluster[idx].nodeList[cluster[idx].nodeNumber].bandwidth = -1;
	cluster[idx].nodeNumber++;
}

void FallbackOrderAgent::nodeClustering(vector<NodeInfo> &sorted, 
		vector<vector<NodeInfo>> &nodeInfo)
{
	int nodeId;
	int maxClusterIdx = 0;
	int base = systemNodeNumber - 1;
	Cluster *cluster = (Cluster *)calloc(
			systemNodeNumber, sizeof(Cluster));
	for (nodeId = 0; nodeId < systemNodeNumber; nodeId++) {
		cluster[nodeId].nodeList = (NodeInfo *)calloc(
				systemNodeNumber, sizeof(NodeInfo));
	}
	setCluster(cluster, maxClusterIdx, sorted[base].nodeId);

	for (int i = systemNodeNumber - 2; i >= 0; i--) {
		if (sorted[base].bandwidth * (100.0f - TOLERANCE) >
				sorted[i].bandwidth * 100.0f) {
			base = i;
			maxClusterIdx++;
		}
		setCluster(cluster, maxClusterIdx, sorted[i].nodeId);
	}
	maxClusterIdx++;
	for(int sid = 0, cnt = 0; sid < systemSocketNumber; sid++, cnt = 0) {
		for (int cid = 0; cid < maxClusterIdx; cid++) {
			int size = cluster[cid].nodeNumber;
			for (int i=0;i<size;i++) {
				cluster[cid].nodeList[i].bandwidth =
					numa_distance(sid, cluster[cid].nodeList[i].nodeId);
			}

			qsort(cluster[cid].nodeList, size,
					sizeof(NodeInfo), compare);

			for (int i=0;i<size;i++) {
				nodeInfo[sid][cnt].nodeId =
					cluster[cid].nodeList[i].nodeId;
				nodeInfo[sid][cnt].bandwidth =
					cluster[cid].nodeList[i].bandwidth;
				cnt++;
			}
		}
	}
}

void FallbackOrderAgent::updateFallbackOrderByBandwidth()
{
	int nodeId;

	vector<NodeInfo> sorted(systemNodeNumber);
	for (nodeId = 0; nodeId < systemNodeNumber; nodeId++) {
		sorted[nodeId].nodeId = nodeId;
		sorted[nodeId].bandwidth = (int)maxBandwidthInfo[nodeId][2];
	}

	qsort(&sorted[0], systemNodeNumber, sizeof(NodeInfo), compare);

	vector<vector<NodeInfo>> nodeInfo(systemSocketNumber);
	for (nodeId = 0; nodeId < systemSocketNumber; nodeId++) {
		nodeInfo[nodeId].resize(systemNodeNumber);
	}

	nodeClustering(sorted, nodeInfo);

	for (nodeId = 0; nodeId < systemSocketNumber; nodeId++) {
		write(nodeInfo[nodeId], nodeId, 0, systemNodeNumber, 0);
	}
}

int FallbackOrderAgent::write(vector<NodeInfo> list, int sid, int start, int end, int idx)
{
	FILE *fp = fopen(managedFileList[sid][idx].c_str(), "w");
	while (1) {
		if (start == end) {
			fprintf(fp, "\n");
			break;
		}
		fprintf(fp, start + 1 == end ? "%d" : "%d,", list[start].nodeId);
		start += 1;
	}
	fclose(fp);
	return 0;
}

void FallbackOrderAgent::update(const ArchMonitor& monitorWorker)
{
	maxBandwidthInfo = monitorWorker.maxBandwidthInfo;
	updateFallbackOrderByBandwidth();
	updateFallbackOrderByTier();
}

