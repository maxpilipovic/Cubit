#include "SoundCues.h"

#include <algorithm>
#include <cmath>

namespace CubitGame
{
    namespace
    {
        //A cell's sound comes from its middle, not its corner.
        glm::vec3 CentreOf(const glm::ivec3& cell)
        {
            return glm::vec3(cell) + glm::vec3(0.5f);
        }

        //Distance summed over many small frames comes out a hair short of the
        //same distance covered at once; this much short still counts.
        constexpr float StrideTolerance = 1e-4f;
    }

    void SoundCues::Shots(const std::deque<MatchClient::ShotReport>& recent, PlayerId local,
        const Locate& locate, std::vector<CueToPlay>& out)
    {
        for (const MatchClient::ShotReport* shot : m_Shots.Take(recent))
        {
            const std::optional<glm::vec3> shooter = locate(shot->Shooter);
            out.push_back({ Cue::Gunshot, true, shooter.value_or(shot->Impact) });
            out.push_back({ Cue::Impact, true, shot->Impact });

            if (shot->Shooter != local || shot->Victim == InvalidPlayer)
                continue;

            out.push_back({ shot->Killed ? Cue::KillConfirm : Cue::HitConfirm, false });
        }
    }

    void SoundCues::Edits(const std::deque<MatchClient::ShownEdit>& recent,
        std::vector<CueToPlay>& out)
    {
        for (const MatchClient::ShownEdit* shown : m_Edits.Take(recent))
            Edited(shown->Edits, out);
    }

    void SoundCues::Edited(std::span<const BlockEdit> edits, std::vector<CueToPlay>& out)
    {
        if (edits.empty())
            return;

        if (edits.size() == 1)
        {
            const BlockEdit& edit = edits.front();
            const Cue sound = edit.Block == BlockId{ 0 } ? Cue::Dig : Cue::Place;
            out.push_back({ sound, true, CentreOf(edit.Position) });
            return;
        }

        glm::vec3 sum(0.0f);
        for (const BlockEdit& edit : edits)
            sum += CentreOf(edit.Position);

        out.push_back({ Cue::Crumble, true, sum / static_cast<float>(edits.size()) });
    }

    void SoundCues::Walk(PlayerId player, const glm::vec3& feet, bool walking, float volume,
        std::vector<CueToPlay>& out)
    {
        const auto [found, first] = m_Gaits.try_emplace(player, Gait{ feet, 0.0f });
        Gait& gait = found->second;

        if (first)
            return;

        const glm::vec2 step(feet.x - gait.Last.x, feet.z - gait.Last.z);
        const float distance = glm::length(step);
        gait.Last = feet;

        if (!walking || distance > Teleport)
            return;

        gait.Travelled += distance;
        if (gait.Travelled + StrideTolerance < Stride)
            return;

        //One step however far past the stride this frame went: a frame long
        //enough to cover two strides is a hitch, and two taps at once would
        //sound like one anyway.
        gait.Travelled = std::fmod(std::max(gait.Travelled - Stride, 0.0f), Stride);

        CueToPlay footstep{ Cue::Footstep, true, feet, volume };
        footstep.Variant = m_Steps++ % FootstepVariants;
        out.push_back(footstep);
    }

    void SoundCues::WalkInferred(PlayerId player, const glm::vec3& feet, float volume,
        std::vector<CueToPlay>& out)
    {
        const auto found = m_Gaits.find(player);
        const bool level = found != m_Gaits.end()
            && std::abs(feet.y - found->second.Last.y) < LevelTolerance;

        Walk(player, feet, level, volume, out);
    }

    void SoundCues::SkipHistory(const std::deque<MatchClient::ShotReport>& shots,
        const std::deque<MatchClient::ShownEdit>& edits)
    {
        m_Shots.SkipTo(shots);
        m_Edits.SkipTo(edits);
    }
}
