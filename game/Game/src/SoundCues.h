#pragma once

#include "NewEvents.h"
#include "SoundSynth.h"

#include "Cubit/Net/MatchClient.h"
#include "Cubit/Voxel/BlockEdit.h"
#include "Cubit/Voxel/MatchState.h"

#include <glm/glm.hpp>

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

//Cubit's own game: deciding what should be heard.
//
//Pure logic: what happened goes in, sounds to play come out, and nothing here
//touches an AudioEngine - which is what lets GameTests cover it. Positions are
//world positions, the same space as the blocks.
namespace CubitGame
{
    struct CueToPlay
    {
        Cue Sound = Cue::Impact;

        //False for a UI sound: heard centred, wherever the listener is.
        bool Positioned = true;
        glm::vec3 Position{ 0.0f };
        float Volume = 1.0f;

        //Which of a cue's clips to play, for cues with more than one.
        int Variant = 0;
    };

    class SoundCues
    {
    public:
        //Horizontal metres between footsteps.
        static constexpr float Stride = 1.6f;

        //Further than this in one frame is a respawn, not a walk.
        static constexpr float Teleport = 4.0f;

        //How far a remote player's height may move in one frame and still
        //count as level ground.
        static constexpr float LevelTolerance = 0.05f;

        //Where a player is, or nothing when this client cannot place them.
        using Locate = std::function<std::optional<glm::vec3>(PlayerId)>;

        //A gunshot at the shooter and an impact where it landed for every new
        //ruling, plus a confirm when the local player hit somebody.
        //
        //A shooter who cannot be placed is heard at the impact, so a shot is
        //never silent.
        void Shots(const std::deque<MatchClient::ShotReport>& recent, PlayerId local,
            const Locate& locate, std::vector<CueToPlay>& out);

        //A sound for every new edit shown, whoever made it.
        void Edits(const std::deque<MatchClient::ShownEdit>& recent,
            std::vector<CueToPlay>& out);

        //A sound for one change to the world. One cell digs or places; several
        //at once - a collapse, a blast - crumble, once, at their centre.
        void Edited(std::span<const BlockEdit> edits, std::vector<CueToPlay>& out);

        //A footstep at `feet` each Stride of horizontal distance a player
        //covers while `walking`. Call once a frame per player.
        //
        //`volume` is the caller's: the local player's own steps are heard
        //from the listener's feet at full strength and want turning down.
        void Walk(PlayerId player, const glm::vec3& feet, bool walking, float volume,
            std::vector<CueToPlay>& out);

        //Walk, for a player whose grounded state this client does not know:
        //walking while their height changed by less than LevelTolerance since
        //the last call. Someone swimming at a steady depth therefore makes
        //footsteps; the fix is a movement bit in the snapshot.
        void WalkInferred(PlayerId player, const glm::vec3& feet, float volume,
            std::vector<CueToPlay>& out);

        //Marks the histories as already heard. For joining: what happened
        //before this client arrived is not news.
        void SkipHistory(const std::deque<MatchClient::ShotReport>& shots,
            const std::deque<MatchClient::ShownEdit>& edits);

    private:
        struct Gait
        {
            glm::vec3 Last{ 0.0f };
            float Travelled = 0.0f;
        };

        NewEvents m_Shots;
        NewEvents m_Edits;
        std::map<PlayerId, Gait> m_Gaits;
        int m_Steps = 0;
    };
}
