#include "AgentFactory.hpp"

AgentFactory::AgentFactory(int _systemSocketNumber, int _systemNodeNumber,
			vector<string> nodeDirectoryPath)
{
	systemSocketNumber = _systemSocketNumber;
	systemNodeNumber = _systemNodeNumber;

	agents.push_back(make_pair(new NodeSaturationAgent(
			systemSocketNumber, systemNodeNumber, nodeDirectoryPath), VALID));
	agents.push_back(make_pair(new MaxBandwidthAgent(
			systemSocketNumber, systemNodeNumber, nodeDirectoryPath), VALID));
	agents.push_back(make_pair(new RemainBandwidthAgent(
			systemSocketNumber, systemNodeNumber, nodeDirectoryPath), VALID));
	agents.push_back(make_pair(new FallbackOrderAgent(
			systemSocketNumber, systemNodeNumber, nodeDirectoryPath), VALID));
	
	for (auto &agent : agents) {
		if (agent.first->createManagedFiles())
			agent.second = INVALID;
	}
}

AgentInterface *AgentFactory::createAgent(int idx)
{
	if (agents[idx].second == INVALID)
		return nullptr;

	return agents[idx].first;
}
