#include "cub.h"

#include "Cubit/Net/SimulatedTransport.h"

#include <algorithm>

SimulatedTransport::SimulatedTransport(Transport& inner, const NetworkSim& sim)
    : m_Inner(inner), m_Sim(sim)
{
    //A zero seed would leave xorshift stuck at zero forever, turning every
    //draw into the same value and every "random" loss into no loss at all.
    m_RandomState = sim.Seed == 0 ? 1 : sim.Seed;
}

void SimulatedTransport::Send(PeerId peer, std::span<const std::uint8_t> data, Channel channel)
{
    Queue(peer, false, data, channel);
}

void SimulatedTransport::Broadcast(std::span<const std::uint8_t> data, Channel channel)
{
    Queue(InvalidPeer, true, data, channel);
}

void SimulatedTransport::Queue(PeerId peer, bool broadcast,
    std::span<const std::uint8_t> data, Channel channel)
{
    double due = m_Now + m_Sim.Latency;

    if (m_Sim.Jitter > 0.0)
    {
        //Uniform across +/- Jitter. Packets can therefore arrive out of order,
        //which is realistic and is exactly why InputMessage carries a sequence.
        due += (NextUnit() * 2.0 - 1.0) * m_Sim.Jitter;
    }

    if (m_Sim.Loss > 0.0f && NextUnit() < static_cast<double>(m_Sim.Loss))
    {
        if (channel == Channel::Unreliable)
            return;

        //Reliable traffic is retransmitted rather than lost. One extra
        //round trip is the cheapest honest model of that, and it keeps the
        //cost of reliability visible in the tests instead of free.
        due += 2.0 * m_Sim.Latency;
    }

    //Never before now, even if jitter pushed it backwards past the send.
    due = std::max(due, m_Now);

    Pending pending;
    pending.Due = due;
    pending.Serial = m_NextSerial++;
    pending.Peer = peer;
    pending.IsBroadcast = broadcast;
    pending.Sent = channel;
    pending.Data.assign(data.begin(), data.end());
    m_Outbound.push_back(std::move(pending));
}

void SimulatedTransport::Disconnect(PeerId peer)
{
    //Not delayed, and deliberately so. A disconnect is a local decision about
    //a connection this end is abandoning, not a packet whose travel time is
    //being modelled. Queueing it would leave a rejected client being simulated
    //for another 50 ms.
    m_Inner.Disconnect(peer);
}

bool SimulatedTransport::Poll(NetEvent& out)
{
    if (m_Inbox.empty())
        return false;

    out = std::move(m_Inbox.front());
    m_Inbox.pop_front();
    return true;
}

void SimulatedTransport::Advance(double seconds)
{
    m_Now += seconds;

    //Everything now due, in a total order: by time, then by send order. The
    //partition keeps the not-yet-due entries without rebuilding the vector.
    std::vector<Pending> due;
    const auto split = std::stable_partition(m_Outbound.begin(), m_Outbound.end(),
        [this](const Pending& pending) { return pending.Due <= m_Now; });

    due.assign(std::make_move_iterator(m_Outbound.begin()), std::make_move_iterator(split));
    m_Outbound.erase(m_Outbound.begin(), split);

    std::sort(due.begin(), due.end(),
        [](const Pending& a, const Pending& b)
        {
            if (a.Due != b.Due)
                return a.Due < b.Due;
            return a.Serial < b.Serial;
        });

    for (const Pending& pending : due)
    {
        if (pending.IsBroadcast)
            m_Inner.Broadcast(pending.Data, pending.Sent);
        else
            m_Inner.Send(pending.Peer, pending.Data, pending.Sent);
    }

    m_Inner.Advance(seconds);

    //Inbound is taken straight through. The other endpoint's own
    //SimulatedTransport already delayed it on the way out; delaying it again
    //here would double the latency of every packet in the system.
    NetEvent event;
    while (m_Inner.Poll(event))
        m_Inbox.push_back(std::move(event));
}

double SimulatedTransport::RoundTripTime(PeerId peer) const
{
    (void)peer;

    //Both ends are assumed to be configured alike, which is true of every
    //caller: the tests build them from one NetworkSim and the Sandbox passes
    //one flag. Reported rather than measured because there is nothing to
    //measure against - no acknowledgement exists until Stage 3.
    return 2.0 * m_Sim.Latency;
}

std::uint64_t SimulatedTransport::NextRandom()
{
    //xorshift64*, Vigna. Small, fast, and identical everywhere, which is the
    //only property that matters here.
    m_RandomState ^= m_RandomState >> 12;
    m_RandomState ^= m_RandomState << 25;
    m_RandomState ^= m_RandomState >> 27;
    return m_RandomState * 0x2545F4914F6CDD1Dull;
}

double SimulatedTransport::NextUnit()
{
    //53 bits is the whole mantissa of a double, so this spans [0, 1) evenly
    //without the modulo bias a smaller shift would introduce.
    return static_cast<double>(NextRandom() >> 11) / 9007199254740992.0;
}
