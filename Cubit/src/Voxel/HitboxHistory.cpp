#include "cub.h"

#include "Cubit/Voxel/HitboxHistory.h"

namespace
{
    Aabb BoxOf(const glm::vec3& position, const glm::vec3& halfExtents)
    {
        return Aabb{ position - halfExtents, position + halfExtents };
    }
}

void HitboxHistory::Record(PlayerId player, std::uint64_t tick, const glm::vec3& position,
    const glm::vec3& halfExtents)
{
    std::deque<Sample>& samples = m_Samples[player];
    samples.push_back(Sample{ tick, position, halfExtents });

    if (samples.size() > MaxHistorySamples)
        samples.pop_front();
}

void HitboxHistory::Forget(PlayerId player)
{
    m_Samples.erase(player);
}

bool HitboxHistory::BoxAt(PlayerId player, double instant, Aabb& out) const
{
    const auto found = m_Samples.find(player);
    if (found == m_Samples.end() || found->second.empty())
        return false;

    const std::deque<Sample>& samples = found->second;

    //Older than anything kept is NOT a hit at the oldest position. There is no
    //record, and saying so is what stops a shot rewinding across a respawn.
    if (instant < static_cast<double>(samples.front().Tick))
        return false;

    const Sample& newest = samples.back();
    if (instant >= static_cast<double>(newest.Tick))
    {
        out = BoxOf(newest.Position, newest.HalfExtents);
        return true;
    }

    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const Sample& previous = samples[i - 1];
        const Sample& next = samples[i];

        if (instant > static_cast<double>(next.Tick))
            continue;

        const double span = static_cast<double>(next.Tick - previous.Tick);
        const float t = span <= 0.0
            ? 0.0f
            : static_cast<float>((instant - static_cast<double>(previous.Tick)) / span);

        //Corners, not a centre and a size: across a crouch the box shrinks
        //from the top while the feet stay put, which lerping the corners keeps.
        const Aabb from = BoxOf(previous.Position, previous.HalfExtents);
        const Aabb to = BoxOf(next.Position, next.HalfExtents);
        out = Aabb{ glm::mix(from.Min, to.Min, t), glm::mix(from.Max, to.Max, t) };
        return true;
    }

    out = BoxOf(newest.Position, newest.HalfExtents);
    return true;
}

std::size_t HitboxHistory::SampleCount(PlayerId player) const
{
    const auto found = m_Samples.find(player);
    return found == m_Samples.end() ? 0 : found->second.size();
}
