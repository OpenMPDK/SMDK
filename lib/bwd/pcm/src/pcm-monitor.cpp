#include "cpucounters.h"
#include "utils.h"

using namespace std;
using namespace pcm;
constexpr uint32 max_sockets = 256;
uint32 max_imc_channels = ServerUncoreCounterState::maxChannels;

void pcm_monitor_calculate_bandwidth(
		const ServerUncoreCounterState uncState1,
		const ServerUncoreCounterState uncState2,
		const uint64 elapsedTime, uint32 socket_id,
		float *read, float *write);

typedef struct memdata {
	float iMC_Rd_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
	float iMC_Wr_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels]{};
	float iMC_Rd_socket[max_sockets]{};
	float iMC_Wr_socket[max_sockets]{};
} memdata_t;

extern "C" {
	ServerUncoreCounterState *BeforeState;
	ServerUncoreCounterState *AfterState;
	uint64 *BeforeTimes;
	uint64 *AfterTimes;
	PCM *m;

	// PCM Instance Initialization
	int pcm_monitor_init()
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

		BeforeState = new ServerUncoreCounterState[m->getNumSockets()];
		AfterState = new ServerUncoreCounterState[m->getNumSockets()];
		BeforeTimes = new uint64[m->getNumSockets()];
		AfterTimes = new uint64[m->getNumSockets()];

		m->setBlocked(false);

        return 0;
    }

	void pcm_monitor_start()
	{
		for (uint32 i = 0; i < m->getNumSockets(); ++i) {
			BeforeTimes[i] = m->getTickCount();
			BeforeState[i] = m->getServerUncoreCounterState(i);
		}
	}

	void pcm_monitor_stop()
	{
		delete[] BeforeState;
		delete[] AfterState;
		delete[] BeforeTimes;
		delete[] AfterTimes;
	}

	int pcm_monitor_get_BW(int socket_id, float *read, float *write)
	{
		if (socket_id < 0 || (uint32)socket_id >= m->getNumSockets())
			return -EINVAL;

		AfterTimes[socket_id] = m->getTickCount();
		AfterState[socket_id] = m->getServerUncoreCounterState(socket_id);

		pcm_monitor_calculate_bandwidth(
					BeforeState[socket_id], AfterState[socket_id],
					AfterTimes[socket_id] - BeforeTimes[socket_id], 
					(uint32)socket_id, read, write);

		swap(BeforeState[socket_id], AfterState[socket_id]);
		swap(BeforeTimes[socket_id], AfterTimes[socket_id]);

		return 0;
	}
}

#define toBW(elapsedTime, nEvents) \
		(float)(nEvents * 64 / 1000000.0 / (elapsedTime / 1000.0))

void pcm_monitor_calculate_bandwidth(
		const ServerUncoreCounterState uncState1,
		const ServerUncoreCounterState uncState2,
		const uint64 elapsedTime, uint32 socket_id,
		float *read, float *write)
{
	memdata_t md;
	md.iMC_Rd_socket[socket_id] = 0.0f;
	md.iMC_Wr_socket[socket_id] = 0.0f;

	for (uint32 channel = 0; channel < max_imc_channels; channel++) {
		uint64 reads = 0, writes = 0;
		reads = getMCCounter(channel, ServerUncorePMUs::EventPosition::READ,
					uncState1, uncState2);

		writes = getMCCounter(channel,ServerUncorePMUs::EventPosition::WRITE,
					uncState1, uncState2);

		md.iMC_Rd_socket_chan[socket_id][channel] = toBW(elapsedTime, reads);
		md.iMC_Wr_socket_chan[socket_id][channel] = toBW(elapsedTime, writes);

		md.iMC_Rd_socket[socket_id] += md.iMC_Rd_socket_chan[socket_id][channel];
		md.iMC_Wr_socket[socket_id] += md.iMC_Wr_socket_chan[socket_id][channel];
	}

	*read = md.iMC_Rd_socket[socket_id];
	*write = md.iMC_Wr_socket[socket_id];
}
