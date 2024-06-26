#ifndef FILE_FACTORY_H
#define FILE_FACTORY_H

#include <vector>

#include "AgentInterface.hpp"
#include "NodeSaturationAgent.hpp"
#include "MaxBandwidthAgent.hpp"
#include "RemainBandwidthAgent.hpp"
#include "FallbackOrderAgent.hpp"

using namespace std;

class AgentFactory
{
	private:
		const int NODE_DIR_LEN = 2048;
		const bool INVALID = false;
		const bool VALID = true;

		int systemSocketNumber;
		int systemNodeNumber;

		vector<pair<AgentInterface *, bool>> agents;

	public:
		AgentFactory(int _systemSocketNumber, int _systemNodeNumber, 
				vector<string> nodeDirectoryPath);
		AgentInterface *createAgent(int idx);

	protected:
};

#endif
