#include "AMDMonitor.hpp"

using namespace std;

int ArchMonitor::uprof_set_num_of_socket(char *line)
{
	if (!line)
		return ERROR;

	if (uprof_skt_num != -1)
		return FINISH;

	int cnt = 0, idx = 0;
	int ret = CONTINUE;
	int len = (int)strlen(line);

	for (int i = 0; i < len; i++) {
		if (line[i] == ',') {
			idx = i + 1;
			cnt++;
		}
		if (cnt == 2)
			return ret;
	}

	char *socket_line = strstr(line, "Number of Sockets :,");
	if (socket_line == NULL)
		return ret;

	char socket[16];
	cnt = 0;
	for (int i = idx; i < len; i++) {
		if (IS_DIGIT(line[i]))
			socket[cnt++] = line[i];
	}
	socket[cnt] = '\0';

	uprof_skt_num = atoi(socket);
	ret = FINISH;
	return ret;
}

int ArchMonitor::uprof_set_history()
{
	if (uprof_history != NULL)
		return FINISH;

	uprof_history = (float ***)calloc(UPROF_HISTORY_SIZE, sizeof(float **));
	if (uprof_history == NULL) {
		return ERROR;
	}
	for (int i = 0; i < UPROF_HISTORY_SIZE; i++) {
		uprof_history[i] = (float **)calloc(uprof_skt_num, sizeof(float *));
		if (uprof_history[i] == NULL) {
			return ERROR;
		}
		for (int j = 0; j < uprof_skt_num; j++) {
			uprof_history[i][j] = (float *)calloc(2, sizeof(float *));
			if (uprof_history[i][j] == NULL) {
				return ERROR;
			}
		}
	}

	return FINISH;
}

void ArchMonitor::uprof_destroy_history()
{
	if (uprof_history) {
		for (int i = 0; i < UPROF_HISTORY_SIZE; i++) {
			for (int j = 0; j < uprof_skt_num; j++) {
				if (uprof_history[i][j])
					free(uprof_history[i][j]);
			}
			if (uprof_history[i])
				free(uprof_history[i]);
		}
		free(uprof_history);
		uprof_history = NULL;
	}
}

int ArchMonitor::uprof_set_num_of_channel(char *line)
{
	if (!line)
		return ERROR;

	if (uprof_chl_idx != NULL)
		return FINISH;

	char *package_line = strstr(line, "Package-");
	if (package_line == NULL)
		return CONTINUE;

	uprof_chl_idx = (int *)malloc(sizeof(int) * uprof_skt_num);
	if (uprof_chl_idx == NULL)
		return ERROR;

	int len = (int)strlen(line);
	for (int i = 0; i < len; i++) {
		if (line[i] != ',')
			continue;
		uprof_chl_num++;
	}

	/*
	 * We only want to get socket's [ total bw, read bw, write bw ].
	 * But uProf reports whole channels's b/w like below.
	 * Package-total bw,read bw,write bw,chl A's read bw,chl A's write bw, ...,
	 * So, we save each socket's total bw index.
	 */
	int adjust = 0, skt = 0;
	for (int i = 1; i < len; i++) {
		if (line[i] == line[i - 1] && line[i] == ',')
			continue;

		if (line[i] != ',')
			adjust++;

		// Start Index of each socket's total bw
		if (line[i] == ',' && line[i - 1] != ',') {
			int start = i - adjust - 1;
			uprof_chl_idx[skt] = start;
			skt++;
		}
	}

	// Don't need to keep going to the next process for this line. Just skip.
	return CONTINUE;
}

int ArchMonitor::uprof_check_meta(char *line)
{
	int ret;

	if (uprof_skt_num != -1 && uprof_history != NULL
			&& uprof_chl_idx != NULL) {
		uprof_history_ready = 1;
		return FINISH;
	}

	// Return FINISH or CONTINUE
	ret = uprof_set_num_of_socket(line);
	if (ret == CONTINUE)
		return ret;

	// Return FINISH or ERROR
	ret = uprof_set_history();
	if (ret == ERROR)
		return ret;

	// Return FINISH or CONTINUE or ERROR
	ret = uprof_set_num_of_channel(line);

	return ret;
}

int ArchMonitor::uprof_check_validation(char *line)
{
	/*
	 * We need to check the validation of line. For example,
	 * For example, valid line's number of semicolon is same with header's one.
	 * But, some invalid line's number of semicolon isn't like below.
	 *  "0.00, ...,\n" // Not begin with total bw but has '\n'.
	 */
	int chl_num = 0;
	int len = (int)strlen(line);
	for (int i = 0; i < len; i++) {
		if (line[i] == ',')
			chl_num++;
	}

	// Invalid line
	if (chl_num != uprof_chl_num) {
		cout << "Channel : " << chl_num << endl;
		cout << "Uprof_chl : " << uprof_chl_num << endl;
		return CONTINUE;
	}

	return FINISH;
}

int ArchMonitor::uprof_split_line(char *line, char ***line_split)
{
	/*
	 * Input : "12.00,6.00,6.00,0.00, ...,";
	 * Output :
	 *          [0] : "12.00"
	 *          [1] : "6.00"
	 *          [2] : "6.00"
	 *          [3] : "0.00"
	 *          ... // The number of channels
	 */

	// Don't need to parse unneeded line.
	if (!IS_DIGIT(line[0]))
		return CONTINUE;

	(*line_split) = (char **)calloc(uprof_chl_num, sizeof(char*));
	if ((*line_split) == NULL) {
		return ERROR;
	}
	for (int i = 0; i < uprof_chl_num; i++) {
		(*line_split)[i] = (char *)calloc(16, sizeof(char));
		if ((*line_split)[i] == NULL) {
			return ERROR;
		}
	}

	int row = 0, col = 0;
	int pos = (int)strlen(line);
	for (int i = 0; i < pos; i++) {
		if (line[i] == '\n')
			break;

		if (line[i] == ',') {
			(*line_split)[row++][col] = '\0';
			col = 0;
			continue;
		}
		(*line_split)[row][col++] = line[i];
	}

	return FINISH;
}

void ArchMonitor::uprof_get_bw(char **line_split)
{
	float read_bw, write_bw;
	for (int i = 0; i < uprof_skt_num; i++) {
		int idx = uprof_history_head & (UPROF_HISTORY_SIZE - 1);
		read_bw = strtod(line_split[uprof_chl_idx[i] + 1], NULL) +
			strtod(line_split[uprof_chl_idx[i] + 3], NULL);
		write_bw = strtod(line_split[uprof_chl_idx[i] + 2], NULL) +
			strtod(line_split[uprof_chl_idx[i] + 4], NULL);
		uprof_history[idx][i][READ] = read_bw;
		uprof_history[idx][i][WRITE] = write_bw;
	}
	uprof_history_head++;

	if (line_split) {
		for (int i = 0; i < uprof_chl_num; i++) {
			if (line_split[i])
				free(line_split[i]);
		}
		free(line_split);
		line_split = NULL;
	}
}

int ArchMonitor::init(int interval __attribute__((unused)))
{ 
	uprofThread = make_unique<thread>(&ArchMonitor::uprofLoop, this);

	return 0;
}

void ArchMonitor::start() { }

void ArchMonitor::stop()
{
	uprof_destroy_history();
}

int ArchMonitor::get_BW(int socketId, int cxlPortId, 
		float *readBw, float *writeBw)
{
	if (cxlPortId != -1) {
		*readBw = 0.0f;
		*writeBw = 0.0f;
		return 0;
	}

	if (!uprof_history_ready) {
		*readBw = 0.0f;
		*writeBw = 0.0f;
		return 0;
	}
	if (socketId >= uprof_skt_num || socketId < 0)
		return ERROR;

	int idx = uprof_history_head - 1 + UPROF_HISTORY_SIZE;
	idx &= (UPROF_HISTORY_SIZE - 1);
	// The formula in the upper line will prevent idx from becoming negative.
	if (idx < 0)
		return ERROR;

	*readBw = uprof_history[idx][socketId][READ];
	*writeBw = uprof_history[idx][socketId][WRITE];

	return 0;
}

void ArchMonitor::updateNodeSaturation()
{
	nodeSaturationAgent->update(*this);
}

void ArchMonitor::updateMaxBandwidth()
{
	maxBandwidthAgent->update(*this);
}

void ArchMonitor::updateRemainBandwidth()
{
	remainBandwidthAgent->update(*this);
}

void ArchMonitor::updateFallbackOrder()
{
	fallbackOrderAgent->update(*this);
}

void ArchMonitor::updateHistoryHead()
{
	historyHead++;
	if (historyHead >= maxHistorySize)
		historyHead = 0;
}

ArchMonitor::ArchMonitor(AgentFactory *_agentFactory, InputParser *_inputParser,
		int _systemSocketNumber, int _systemNodeNumber)
{
	cout << "Monitor Constructor" << endl;

	bandwidthHistory.clear();
	capacityHistory.clear();
	maxBandwidthInfo.clear();
	maxCapacityInfo.clear();

	agentFactory = _agentFactory;
	inputParser = _inputParser;
	systemSocketNumber = _systemSocketNumber;
	systemNodeNumber = _systemNodeNumber;

	bandwidthHistory.resize(maxHistorySize);
	for(unsigned int i=0;i<maxHistorySize;i++) {
		bandwidthHistory[i].resize(systemNodeNumber);
		for (int j=0;j<systemNodeNumber;j++) {
			bandwidthHistory[i][j].resize(bandwidthTypeNumber);
		}
	}
	capacityHistory.resize(maxHistorySize);
	for(unsigned int i=0;i<maxHistorySize;i++) {
		capacityHistory[i].resize(systemNodeNumber);
	}
	maxBandwidthInfo.resize(systemNodeNumber);
	for (int i=0;i<systemNodeNumber;i++) {
		maxBandwidthInfo[i].resize(bandwidthTypeNumber);
	}
	maxCapacityInfo.resize(systemNodeNumber);

	stopFlag.store(false);
	workerThread = nullptr;
	uprofThread = nullptr;

	historyHead = 0;

	nodeSaturationAgent = _agentFactory->createAgent(0);
	maxBandwidthAgent = _agentFactory->createAgent(1);
	remainBandwidthAgent = _agentFactory->createAgent(2);
	fallbackOrderAgent = _agentFactory->createAgent(3);
}

void ArchMonitor::uprofLoop()
{
	std::string cmd = inputParser->getPcmPath() + " -m memory -d -1 -a";
	FILE *fp = popen(cmd.c_str(), "r");
	int UPROF_BUF_SIZE = 4096;
	char buffer[UPROF_BUF_SIZE] = { '\0', };
	size_t read_bytes;
	while ((read_bytes = read(fileno(fp), buffer, sizeof(buffer))) != 0) {
		if (stopFlag.load())
			break;

		int max_row = 0;
		for (int i = 0; i < (int)read_bytes; i++) {
			if (buffer[i] == '\n')
				max_row++;
		}

		char **lines = (char **)calloc(max_row, sizeof(char *));
		if (lines == NULL) {
			return;
		}
		for (int i = 0; i < max_row; i++) {
			lines[i] = (char *)calloc(UPROF_BUF_SIZE, sizeof(char));
			if (lines[i] == NULL) {
				return;
			}
		}

		int row = 0, col = 0;
		for (int i = 0; i < (int)read_bytes; i++) {
			/*
			 * '\n' is only sentinel to separate uProf results into line by line.
			 * But we need to check the format of each lines.
			 * For example, format of good line is below.
			 *  "12.00, 6.00, 6.00, 0.00, ..., \n"  // '\n' is exists.
			 * But some wrong line also be like below.
			 *  "12.00, 6.00, ..., 0.0" // '\n' is not exists.
			 * In this case, we discard wrong lines.
			 */
			if (row == max_row)
				break;
			if (buffer[i] == '\n') {
				lines[row++][col] = '\0';
				col = 0;
				continue;
			}
			lines[row][col++] = buffer[i];
		}

		for (int i = 0; i < max_row; i++) {
			char *line = lines[i];
			int ret;

			// Return FINISH or CONTINUE or ERROR
			ret = uprof_check_meta(line);
			if (ret == ERROR) {
				return;
			} else if (ret == CONTINUE) {
				continue;
			}

			// Return FINISH or CONTINUE
			ret = uprof_check_validation(line);
			if (ret == CONTINUE) {
				continue;
			}

			// Return FINISH or ERROR
			char **line_split = NULL;
			ret = uprof_split_line(line, &line_split);
			if (ret == ERROR) {
				return;
			}

			if (ret == CONTINUE) {
				continue;
			}

			// Now we can get bandwidth.
			uprof_get_bw(line_split);
		}
		memset(buffer, '\0', UPROF_BUF_SIZE);
		if (lines) {
			for (int i = 0; i < max_row; i++) {
				if (lines[i])
					free(lines[i]);
			}
			free(lines);
			lines = NULL;
		}
	}
}

void ArchMonitor::updateBandwidthHistory(int nodeId,
		float readBw, float writeBw)
{
	unsigned int bwType;
	vector<float> bwList = { readBw, writeBw, readBw + writeBw };

	for (bwType = 0; bwType < bandwidthTypeNumber; bwType++) {
		float currentBw = bwList[bwType];
		float currentMax = maxBandwidthInfo[nodeId][bwType];
		bandwidthHistory[historyHead][nodeId][bwType] = currentBw;
		if (currentBw > currentMax) {
			maxBandwidthInfo[nodeId][bwType] = currentBw;
		}
	}
}

void ArchMonitor::updateCapacityHistory(int nodeId)
{
	long long freeSize;
	long long totalSize;

	totalSize = numa_node_size64(nodeId, &freeSize);
	if (totalSize == -1L) {
		maxCapacityInfo[nodeId] = 0;
		return;
	}

	maxCapacityInfo[nodeId] = totalSize;
	capacityHistory[historyHead][nodeId] = freeSize;
}

void ArchMonitor::monitorLoop()
{
	int nodeId;

	init(inputParser->getInterval());

	start();
	while (1) {
		if (stopFlag.load())
			break;

		for (nodeId = 0; nodeId < systemNodeNumber; nodeId++) {
			float readBw = 0.0f;
			float writeBw = 0.0f;

			int cxlPortId = 0;
			if (nodeId < systemSocketNumber) {
				cxlPortId = -1;
			}

			if (get_BW(nodeId, cxlPortId, &readBw, &writeBw)) {
				cerr << "Monitor Error: get_BW" << endl;
				break;
			}

			if (readBw > SPIKE || isnan(readBw) ||
					writeBw > SPIKE || isnan(writeBw)) {
				readBw = 0.0;
				writeBw = 0.0;
			}

			auto now = std::chrono::system_clock::now();
			time_t now_time = std::chrono::system_clock::to_time_t(now);
			std::tm *local_time = std::localtime(&now_time);

			cout << "[" << std::put_time(local_time, "%H:%M:%S") << "]";
			cout << " " << "[" << nodeId << "]" << " ";
			cout << readBw << " " << writeBw << " " << readBw + writeBw << endl;
			updateBandwidthHistory(nodeId, readBw, writeBw);
			updateCapacityHistory(nodeId);
		}

		updateNodeSaturation();
		updateMaxBandwidth();
		updateRemainBandwidth();

		updateHistoryHead();

		sleep((int)(inputParser->getInterval() / 1000000));
	}
	stop();
}

int ArchMonitor::launch()
{
	cout << "Launch Worker" << endl;

	workerThread = make_unique<thread>(&ArchMonitor::monitorLoop, this);

	return 0;
}

int ArchMonitor::terminate()
{
	stopFlag.store(true);
	if (workerThread && workerThread->joinable()) {
		workerThread->join();
	}
	if (uprofThread && uprofThread->joinable()) {
		uprofThread->join();
	}

	cout << "Terminate Monitor" << endl;

	return 0;
}

void ArchMonitor::notifyFromSignalHandlerStop()
{
	terminate();
}

void ArchMonitor::notifyFromSignalHandlerChange()
{
	SystemInfo systemInfo;
	systemSocketNumber = systemInfo.getSystemNodeNumber();
	systemNodeNumber = systemInfo.getSystemSocketNumber();

	historyHead = 0;

	bandwidthHistory.clear();
	capacityHistory.clear();
	maxBandwidthInfo.clear();
	maxCapacityInfo.clear();

	bandwidthHistory.resize(maxHistorySize);
	for(unsigned int i=0;i<maxHistorySize;i++) {
		bandwidthHistory[i].resize(systemNodeNumber);
		for (int j=0;j<systemNodeNumber;j++) {
			bandwidthHistory[i][j].resize(bandwidthTypeNumber);
		}
	}
	capacityHistory.resize(maxHistorySize);
	for(unsigned int i=0;i<maxHistorySize;i++) {
		capacityHistory[i].resize(systemNodeNumber);
	}
	maxBandwidthInfo.resize(systemNodeNumber);
	for (int i=0;i<systemNodeNumber;i++) {
		maxBandwidthInfo[i].resize(bandwidthTypeNumber);
	}
	maxCapacityInfo.resize(systemNodeNumber);
}

void ArchMonitor::notifyFromBandwidthLoader()
{
	updateFallbackOrder();
}
