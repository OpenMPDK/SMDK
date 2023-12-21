#include "cpucounters.h"
#include "utils.h"

using namespace std;
using namespace pcm;
constexpr uint32 max_sockets = 256;
uint32 max_imc_channels = ServerUncoreCounterState::maxChannels;

void pcm_monitor_calculate_bandwidth(
		const std::vector<ServerUncoreCounterState>& uncState1,
		const std::vector<ServerUncoreCounterState>& uncState2,
		const uint64 elapsedTime, uint32 socket_id, int port_id,
		float *read, float *write, const uint64 SPR_CHA_CXL_Count);

typedef std::vector<uint64> eventGroup_t;
typedef struct memdata {
	float iMC_Rd_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
	float iMC_Wr_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
	float iMC_Rd_socket[max_sockets]{};
	float iMC_Wr_socket[max_sockets]{};

	float CXLMEM_Wr_socket_port[max_sockets][ServerUncoreCounterState::maxCXLPorts]{};
} memdata_t;

uint32 getNumCXLPorts(PCM* m)
{
	static int numPorts = -1;
	if (numPorts < 0) {
		for (uint32 s = 0; s < m->getNumSockets(); s++) {
			numPorts = (std::max)(numPorts, (int)m->getNumCXLPorts(s));
		}
		assert(numPorts >= 0);
	}
	return (uint32)numPorts;
}

void readState(std::vector<ServerUncoreCounterState> &state)
{
	auto *pcm = PCM::getInstance();
	assert(pcm);
	for (uint32 i = 0; i < pcm->getNumSockets(); i++) {
		state[i] = pcm->getServerUncoreCounterState(i);
	}
}

void simpleCalibratedSleep(const double delay, PCM *m)
{
	static uint64 TimeAfterSleep = 0;
	int delay_ms = int(delay * 1000);

	if (TimeAfterSleep)
		delay_ms -= (int)(m->getTickCount() - TimeAfterSleep);

	if (delay_ms < 0)
		delay_ms = 0;

	if (delay_ms > 0)
		MySleepMs(delay_ms);

	TimeAfterSleep = m->getTickCount();
}

class CHAEventCollector
{
	std::vector<eventGroup_t> eventGroups;
	double delay;
	PCM* pcm;
	std::vector<std::vector<ServerUncoreCounterState>> MidStates;
	size_t curGroup = 0ULL;
	uint64 totalCount = 0ULL;
	CHAEventCollector() = delete;
	CHAEventCollector(const CHAEventCollector&) = delete;
	CHAEventCollector & operator = (const CHAEventCollector &) = delete;

	uint64 extractCHATotalCount(const std::vector<ServerUncoreCounterState>& before, const std::vector<ServerUncoreCounterState>& after)
	{
		uint64 result = 0;
		for (uint32 i = 0; i < pcm->getNumSockets(); ++i)
		{
			for (uint32 cbo = 0; cbo < pcm->getMaxNumOfCBoxes(); ++cbo)
			{
				for (uint32 ctr = 0; ctr < 4 && ctr < eventGroups[curGroup].size(); ++ctr)
				{
					result += getCBOCounter(cbo, ctr, before[i], after[i]);
				}
			}
		}
		return result;
	}

	void programGroup(const size_t group)
	{
		uint64 events[4] = { 0, 0, 0, 0 };
		assert(group < eventGroups.size());
		for (size_t i = 0; i < 4 && i < eventGroups[group].size(); ++i)
		{
			events[i] = eventGroups[group][i];
		}
		pcm->programCboRaw(events, 0, 0);
	}

public:
	CHAEventCollector(const double delay_, PCM* m) :
		pcm(m)
	{
		assert(pcm);
		switch (pcm->getCPUModel())
		{
			case PCM::SPR:
				eventGroups = {
					{
						UNC_PMON_CTL_EVENT(0x35) + UNC_PMON_CTL_UMASK(0x01) + UNC_PMON_CTL_UMASK_EXT(0x10C80B82) , // UNC_CHA_TOR_INSERTS.IA_MISS_CRDMORPH_CXL_ACC
						UNC_PMON_CTL_EVENT(0x35) + UNC_PMON_CTL_UMASK(0x01) + UNC_PMON_CTL_UMASK_EXT(0x10c80782) , // UNC_CHA_TOR_INSERTS.IA_MISS_RFO_CXL_ACC
						UNC_PMON_CTL_EVENT(0x35) + UNC_PMON_CTL_UMASK(0x01) + UNC_PMON_CTL_UMASK_EXT(0x10c81782) , // UNC_CHA_TOR_INSERTS.IA_MISS_DRD_CXL_ACC
						UNC_PMON_CTL_EVENT(0x35) + UNC_PMON_CTL_UMASK(0x01) + UNC_PMON_CTL_UMASK_EXT(0x10C88782)   // UNC_CHA_TOR_INSERTS.IA_MISS_LLCPREFRFO_CXL_ACC
					},
					{
						UNC_PMON_CTL_EVENT(0x35) + UNC_PMON_CTL_UMASK(0x01) + UNC_PMON_CTL_UMASK_EXT(0x10CCC782) , // UNC_CHA_TOR_INSERTS.IA_MISS_RFO_PREF_CXL_ACC
						UNC_PMON_CTL_EVENT(0x35) + UNC_PMON_CTL_UMASK(0x01) + UNC_PMON_CTL_UMASK_EXT(0x10C89782) , // UNC_CHA_TOR_INSERTS.IA_MISS_DRD_PREF_CXL_ACC
						UNC_PMON_CTL_EVENT(0x35) + UNC_PMON_CTL_UMASK(0x01) + UNC_PMON_CTL_UMASK_EXT(0x10CCD782) , // UNC_CHA_TOR_INSERTS.IA_MISS_LLCPREFDATA_CXL_ACC
						UNC_PMON_CTL_EVENT(0x35) + UNC_PMON_CTL_UMASK(0x01) + UNC_PMON_CTL_UMASK_EXT(0x10CCCF82)   // UNC_CHA_TOR_INSERTS.IA_MISS_LLCPREFCODE_CXL_ACC
					}
				};
				break;
		}

		assert(eventGroups.size() > 1);

		delay = delay_ / double(eventGroups.size());
		MidStates.resize(eventGroups.size() - 1);
		for (auto& e : MidStates)
		{
			e.resize(pcm->getNumSockets());
		}
	}

	void programFirstGroup()
	{
		programGroup(0);
	}

	void multiplexEvents(const std::vector<ServerUncoreCounterState>& BeforeState)
	{
		for (curGroup = 0; curGroup < eventGroups.size() - 1; ++curGroup)
		{
			assert(curGroup < MidStates.size());
			simpleCalibratedSleep(delay, pcm);
			readState(MidStates[curGroup]);
			totalCount += extractCHATotalCount((curGroup > 0) ? MidStates[curGroup - 1] : BeforeState, MidStates[curGroup]);
			programGroup(curGroup + 1);
			readState(MidStates[curGroup]);
		}
		simpleCalibratedSleep(delay, pcm);
	}

	uint64 getTotalCount(const std::vector<ServerUncoreCounterState>& AfterState)
	{
		return eventGroups.size() * (totalCount + extractCHATotalCount(MidStates.back(), AfterState));
	}

	void reset()
	{
		totalCount = 0;
	}
};

extern "C" {
	double delay = 1.0f; // in second.
	uint64 SPR_CHA_CXL_Event_Count = 0;
	shared_ptr<CHAEventCollector> chaEventCollector;
	std::vector<ServerUncoreCounterState> BeforeState;
	std::vector<ServerUncoreCounterState> AfterState;
	uint64 BeforeTime;
	uint64 AfterTime;
	PCM *m;

	// PCM Instance Initialization
	int pcm_monitor_init(int interval_in_us)
	{
		m = PCM::getInstance();

		ServerUncoreMemoryMetrics metrics =
			m->PMMTrafficMetricsAvailable() ? Pmem : PartialWrites;

		// For high accuracy in Sandy Bridge.
		m->disableJKTWorkaround();

		PCM::ErrorCode status;
		/*
		 * The second and third parameter only for Ivytown and Skylake
		 * to additional tracking on Memory Channel 0.
		 * The second and third parameter don't affect other CPUs.
		 */
		status = m->programServerUncoreMemoryMetrics(metrics, -1, -1);
		if (status != PCM::Success)
			return status == PCM::PMUBusy ? -EBUSY : -EPERM;

		max_imc_channels = (pcm::uint32)m->getMCChannelsPerSocket();

		BeforeState.resize(m->getNumSockets());
		AfterState.resize(m->getNumSockets());
		BeforeTime = 0;
		AfterTime = 0;

		m->setBlocked(false);

		// 1 second is the minimum value of interval for CXL Read BW Accuracy.
		if (interval_in_us > 1000000)
			delay = (float)(interval_in_us / 1000000.0f);

		bool SPR_CXL = (PCM::SPR == m->getCPUModel()) && (getNumCXLPorts(m) > 0);
		if (SPR_CXL) {
			chaEventCollector = std::make_shared<CHAEventCollector>(delay, m);
			assert(chaEventCollector.get());
			chaEventCollector->programFirstGroup();
		}

		return 0;
	}

	void pcm_monitor_start()
	{
		readState(BeforeState);
		BeforeTime = m->getTickCount();
		SPR_CHA_CXL_Event_Count = 0;
	}

	void pcm_monitor_stop()
	{
		if (PCM::isInitialized())
			PCM::getInstance()->cleanup();
	}

	void pcm_monitor_report_start()
	{
		if (chaEventCollector.get())
			chaEventCollector->multiplexEvents(BeforeState);
		else
			simpleCalibratedSleep(delay, m);

		AfterTime = m->getTickCount();
		readState(AfterState);

		if (chaEventCollector.get()) {
			SPR_CHA_CXL_Event_Count = chaEventCollector->getTotalCount(AfterState);
			chaEventCollector->reset();
			chaEventCollector->programFirstGroup();
			readState(AfterState);
		}
	}

	int pcm_monitor_get_BW(int socket_id, int port_id, float *read, float *write)
	{
		if (socket_id < 0 || (uint32)socket_id >= m->getNumSockets())
			return -EINVAL;

		/*
		 * DDR DRAM Case : port_id must be -1.
		 * CXL DRAM Case : port_id should be smaller than Socket's CXL Ports Num.
		 */
		if (port_id != -1 && port_id >= (int)m->getNumCXLPorts(socket_id))
			return -EINVAL;

		pcm_monitor_calculate_bandwidth(
				BeforeState, AfterState, AfterTime - BeforeTime,
				(uint32)socket_id, port_id, read, write,
				SPR_CHA_CXL_Event_Count);

		return 0;
	}

	void pcm_monitor_report_finish()
	{
		swap(BeforeTime, AfterTime);
		swap(BeforeState, AfterState);
	}
}

constexpr float CXLBWWrScalingFactor = 0.5;

void pcm_monitor_calculate_bandwidth(
		const std::vector<ServerUncoreCounterState>& uncState1,
		const std::vector<ServerUncoreCounterState>& uncState2,
		const uint64 elapsedTime, uint32 socket_id, int port_id,
		float *read, float *write,
		const uint64 SPR_CHA_CXL_Count)
{
	auto toBW = [&elapsedTime](const uint64 nEvents)
	{
		return (float)(nEvents * 64 / 1000000.0 / (elapsedTime / 1000.0));
	};

	memdata_t md;

	if (port_id == -1) {
		md.iMC_Rd_socket[socket_id] = 0.0f;
		md.iMC_Wr_socket[socket_id] = 0.0f;

		for (uint32 channel = 0; channel < max_imc_channels; channel++) {
			uint64 reads = 0, writes = 0;
			reads = getMCCounter(channel, ServerUncorePMUs::EventPosition::READ,
						uncState1[socket_id], uncState2[socket_id]);
			writes = getMCCounter(channel,ServerUncorePMUs::EventPosition::WRITE,
						uncState1[socket_id], uncState2[socket_id]);

			md.iMC_Rd_socket_chan[socket_id][channel] = toBW(reads);
			md.iMC_Wr_socket_chan[socket_id][channel] = toBW(writes);

			md.iMC_Rd_socket[socket_id] +=
				md.iMC_Rd_socket_chan[socket_id][channel];
			md.iMC_Wr_socket[socket_id] +=
				md.iMC_Wr_socket_chan[socket_id][channel];
		}
		*read = md.iMC_Rd_socket[socket_id];
		*write = md.iMC_Wr_socket[socket_id];
	} else {
		md.CXLMEM_Wr_socket_port[socket_id][port_id] =
			CXLBWWrScalingFactor * toBW(getCXLCMCounter((uint32)port_id,
						PCM::EventPosition::CXL_TxC_MEM,
						uncState1[socket_id], uncState2[socket_id]));

		*read = toBW(SPR_CHA_CXL_Count);
		*write = md.CXLMEM_Wr_socket_port[socket_id][port_id];
	}
}
