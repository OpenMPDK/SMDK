#ifndef INTEL_MONITOR_H
#define INTEL_MONITOR_H

#include "SystemInfo.hpp"
#include "InputParser.hpp"
#include "AgentFactory.hpp"
#include "AgentInterface.hpp"
#include "MonitorInterface.hpp"

using namespace std;

// Using PCM Library API
long pcm_monitor_init(int interval_in_us);
void pcm_monitor_start();
void pcm_monitor_stop();
void pcm_monitor_report_start();
int pcm_monitor_get_BW(int socket_id, int port_id, float *read, float *write);
void pcm_monitor_report_finish();

class ArchMonitor : public MonitorInterface
{
	private:
		const unsigned int maxHistorySize = 100;
		const unsigned int bandwidthTypeNumber = 3;
		const float SPIKE = 300000.0;

		int systemSocketNumber;
		int systemNodeNumber;
		InputParser *inputParser;
		AgentFactory *agentFactory;
		AgentInterface *nodeSaturationAgent;
		AgentInterface *maxBandwidthAgent;
		AgentInterface *remainBandwidthAgent;
		AgentInterface *fallbackOrderAgent;

		atomic<bool> stopFlag;
		unique_ptr<thread> workerThread;

		unsigned int historyHead;
		vector<vector<vector<float>>> bandwidthHistory;
		vector<vector<long long>> capacityHistory;
		vector<vector<float>> maxBandwidthInfo;
		vector<long long> maxCapacityInfo;

		int init(int interval_in_us) override;
		void start() override;
		int get_BW(int socketId, int cxlPortId, 
				float *readBw, float *writeBw) override;
		void stop() override;
		void report_start();
		void report_finish();

		void updateBandwidthHistory(int nodeId, float readBw, float writeBw);
		void updateCapacityHistory(int nodeId);
		void updateHistoryHead();

		void updateNodeSaturation();
		void updateMaxBandwidth();
		void updateRemainBandwidth();
		void updateFallbackOrder();

		void print(int nodeId, float readBw, float writeBw);

	public:
		ArchMonitor(AgentFactory *_agentFactory, InputParser *_inputParser,
				int _systemSocketNumber, int _systemNodeNumber);

		int launch() override;
		int terminate() override;
		void notifyFromSignalHandlerStop() override;
		void notifyFromSignalHandlerChange() override;
		void notifyFromBandwidthLoader() override;

		void monitorLoop();

		friend class NodeSaturationAgent;
		friend class MaxBandwidthAgent;
		friend class RemainBandwidthAgent;
		friend class FallbackOrderAgent;

	protected:
};

#endif
