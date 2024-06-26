#ifndef AMD_MONITOR_H
#define AMD_MONITOR_H

#include "SystemInfo.hpp"
#include "InputParser.hpp"
#include "AgentFactory.hpp"
#include "AgentInterface.hpp"
#include "MonitorInterface.hpp"

using namespace std;

class ArchMonitor : public MonitorInterface
{
	private:
		const unsigned int maxHistorySize = 100;
		const unsigned int bandwidthTypeNumber = 3;
		const float SPIKE = 300000.0f;

        int systemSocketNumber;
        int systemNodeNumber;
        InputParser *inputParser;
        AgentFactory *agentFactory;
        AgentInterface *nodeSaturationAgent;
        AgentInterface *maxBandwidthAgent;
        AgentInterface *remainBandwidthAgent;
        AgentInterface *fallbackOrderAgent;

        atomic<bool> stopFlag;
        unique_ptr<thread> uprofThread;
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

        void updateBandwidthHistory(int nodeId, float readBw, float writeBw);
        void updateCapacityHistory(int nodeId);
        void updateHistoryHead();

        void updateNodeSaturation();
        void updateMaxBandwidth();
        void updateRemainBandwidth();
        void updateFallbackOrder();

		FILE *uprof_fp;

		int uprof_chl_num = 0;
		int uprof_skt_num = -1;
		int uprof_history_head = 0;
		bool uprof_history_ready = 0;
		#define IS_DIGIT(x) ('0' <= (x) && (x) <= '9')
		int UPROF_HISTORY_SIZE = 1024;

		int *uprof_chl_idx;
		float ***uprof_history;

		int FINISH = 0;
		int CONTINUE = 1;
		int ERROR = 2;
		int READ = 0;
		int WRITE = 1;
		int uprof_set_num_of_socket(char *line);
		int uprof_set_history();
		void uprof_destroy_history();
		int uprof_set_num_of_channel(char *line);
		int uprof_check_meta(char *line);
		int uprof_check_validation(char *line);
		int uprof_split_line(char *line, char ***line_split);
		void uprof_get_bw(char **line_split);

	public:
		ArchMonitor(AgentFactory *_agentFactory, InputParser *_inputParser,
				int _systemSocketNumber, int _systemNodeNumber);
		int launch() override;
		int terminate() override;
        void notifyFromSignalHandlerStop() override;
        void notifyFromSignalHandlerChange() override;
        void notifyFromBandwidthLoader() override;

        void uprofLoop();
        void monitorLoop();

        friend class NodeSaturationAgent;
        friend class MaxBandwidthAgent;
        friend class RemainBandwidthAgent;
        friend class FallbackOrderAgent;


	protected:
};

#endif
