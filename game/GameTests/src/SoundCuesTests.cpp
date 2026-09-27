#include <doctest.h>

#include "SoundCues.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <vector>

using CubitGame::Cue;
using CubitGame::CueToPlay;
using CubitGame::SoundCues;

namespace
{
    constexpr PlayerId Me = 1;
    constexpr PlayerId Them = 2;

    const glm::vec3 MyEye{ 10.0f, 5.0f, 10.0f };
    const glm::vec3 TheirPosition{ 30.0f, 5.0f, 10.0f };

    MatchClient::ShotReport Shot(std::uint64_t serial, PlayerId shooter,
        PlayerId victim = InvalidPlayer, bool killed = false)
    {
        MatchClient::ShotReport shot;
        shot.Serial = serial;
        shot.Shooter = shooter;
        shot.Victim = victim;
        shot.Killed = killed;
        shot.Impact = glm::vec3(20.0f, 3.0f, static_cast<float>(serial));
        return shot;
    }

    MatchClient::ShownEdit Shown(std::uint64_t serial, std::vector<BlockEdit> edits,
        bool local = false)
    {
        MatchClient::ShownEdit shown;
        shown.Serial = serial;
        shown.Local = local;
        shown.Edits = std::move(edits);
        return shown;
    }

    std::optional<glm::vec3> Where(PlayerId player)
    {
        if (player == Me)
            return MyEye;
        if (player == Them)
            return TheirPosition;
        return std::nullopt;
    }

    std::size_t Count(const std::vector<CueToPlay>& cues, Cue sound)
    {
        return static_cast<std::size_t>(std::count_if(cues.begin(), cues.end(),
            [sound](const CueToPlay& cue) { return cue.Sound == sound; }));
    }

    const CueToPlay* Find(const std::vector<CueToPlay>& cues, Cue sound)
    {
        const auto found = std::find_if(cues.begin(), cues.end(),
            [sound](const CueToPlay& cue) { return cue.Sound == sound; });
        return found == cues.end() ? nullptr : &*found;
    }
}

TEST_CASE("One ruling makes one gunshot at the shooter and one impact where it landed")
{
    SoundCues cues;
    std::vector<CueToPlay> out;

    cues.Shots({ Shot(1, Them) }, Me, Where, out);

    REQUIRE(Count(out, Cue::Gunshot) == 1);
    REQUIRE(Count(out, Cue::Impact) == 1);

    const CueToPlay* gunshot = Find(out, Cue::Gunshot);
    CHECK(gunshot->Positioned);
    CHECK(gunshot->Position == TheirPosition);

    const CueToPlay* impact = Find(out, Cue::Impact);
    CHECK(impact->Positioned);
    CHECK(impact->Position == glm::vec3(20.0f, 3.0f, 1.0f));
}

TEST_CASE("Three rulings in one frame make three of each, and none twice")
{
    SoundCues cues;
    std::vector<CueToPlay> out;

    const std::deque<MatchClient::ShotReport> recent{
        Shot(1, Them), Shot(2, Them), Shot(3, Me) };

    cues.Shots(recent, Me, Where, out);
    CHECK(Count(out, Cue::Gunshot) == 3);
    CHECK(Count(out, Cue::Impact) == 3);

    out.clear();
    cues.Shots(recent, Me, Where, out);
    CHECK(out.empty());
}

TEST_CASE("A shooter nobody can place is heard where the shot landed")
{
    SoundCues cues;
    std::vector<CueToPlay> out;

    cues.Shots({ Shot(1, 99) }, Me, Where, out);

    const CueToPlay* gunshot = Find(out, Cue::Gunshot);
    REQUIRE(gunshot != nullptr);
    CHECK(gunshot->Position == glm::vec3(20.0f, 3.0f, 1.0f));
}

TEST_CASE("The local player's hits and kills confirm, in 2D; other players' do not")
{
    SoundCues cues;
    std::vector<CueToPlay> out;

    cues.Shots({ Shot(1, Me, Them) }, Me, Where, out);
    REQUIRE(Count(out, Cue::HitConfirm) == 1);
    CHECK_FALSE(Find(out, Cue::HitConfirm)->Positioned);
    CHECK(Count(out, Cue::KillConfirm) == 0);

    out.clear();
    cues.Shots({ Shot(1, Me, Them), Shot(2, Me, Them, true) }, Me, Where, out);
    CHECK(Count(out, Cue::KillConfirm) == 1);
    CHECK(Count(out, Cue::HitConfirm) == 0);

    out.clear();
    cues.Shots({ Shot(1, Me, Them), Shot(2, Me, Them, true), Shot(3, Them, Me) },
        Me, Where, out);
    CHECK(Count(out, Cue::HitConfirm) == 0);
    CHECK(Count(out, Cue::KillConfirm) == 0);

    //A miss confirms nothing.
    out.clear();
    cues.Shots({ Shot(1, Me, Them), Shot(2, Me, Them, true), Shot(3, Them, Me), Shot(4, Me) },
        Me, Where, out);
    CHECK(Count(out, Cue::HitConfirm) == 0);
}

TEST_CASE("A dig digs, a placement places, and a batch crumbles once")
{
    SoundCues cues;
    std::vector<CueToPlay> out;

    std::deque<MatchClient::ShownEdit> recent{
        Shown(1, { BlockEdit{ glm::ivec3(4, 2, 6), BlockId{ 0 } } }) };
    cues.Edits(recent, out);

    REQUIRE(out.size() == 1);
    CHECK(out[0].Sound == Cue::Dig);
    CHECK(out[0].Positioned);
    CHECK(out[0].Position == glm::vec3(4.5f, 2.5f, 6.5f));

    recent.push_back(Shown(2, { BlockEdit{ glm::ivec3(1, 1, 1), BlockId{ 3 } } }, true));
    out.clear();
    cues.Edits(recent, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].Sound == Cue::Place);

    recent.push_back(Shown(3, {
        BlockEdit{ glm::ivec3(0, 10, 0), BlockId{ 0 } },
        BlockEdit{ glm::ivec3(2, 10, 0), BlockId{ 0 } },
        BlockEdit{ glm::ivec3(4, 10, 0), BlockId{ 0 } } }));
    out.clear();
    cues.Edits(recent, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].Sound == Cue::Crumble);
    CHECK(out[0].Position == glm::vec3(2.5f, 10.5f, 0.5f));
}

TEST_CASE("A single-player edit makes the same sound as a connected one")
{
    SoundCues cues;
    std::vector<CueToPlay> out;

    const BlockEdit dig{ glm::ivec3(4, 2, 6), BlockId{ 0 } };
    cues.Edited(std::span<const BlockEdit>(&dig, 1), out);

    REQUIRE(out.size() == 1);
    CHECK(out[0].Sound == Cue::Dig);
    CHECK(out[0].Position == glm::vec3(4.5f, 2.5f, 6.5f));
}

TEST_CASE("Footsteps follow distance walked, not frames")
{
    const glm::vec3 start{ 10.0f, 1.0f, 10.0f };

    SUBCASE("one stride in one frame is one step")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        cues.Walk(Me, start, true, 1.0f, out);
        cues.Walk(Me, start + glm::vec3(SoundCues::Stride, 0.0f, 0.0f), true, 1.0f, out);
        CHECK(Count(out, Cue::Footstep) == 1);
    }

    SUBCASE("one stride over a hundred frames is one step")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        for (int i = 0; i <= 100; ++i)
            cues.Walk(Me, start + glm::vec3(SoundCues::Stride * i / 100.0f, 0.0f, 0.0f),
                true, 1.0f, out);

        CHECK(Count(out, Cue::Footstep) == 1);
    }

    SUBCASE("standing still is silent")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        for (int i = 0; i < 100; ++i)
            cues.Walk(Me, start, true, 1.0f, out);

        CHECK(out.empty());
    }

    SUBCASE("moving while not walking - falling, swimming - is silent")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        for (int i = 0; i <= 100; ++i)
            cues.Walk(Me, start + glm::vec3(0.1f * i, 0.0f, 0.0f), false, 1.0f, out);

        CHECK(out.empty());
    }

    SUBCASE("height does not count as distance")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        cues.Walk(Me, start, true, 1.0f, out);
        cues.Walk(Me, start + glm::vec3(0.0f, 1.0f, 0.0f), true, 1.0f, out);
        CHECK(out.empty());
    }

    SUBCASE("a teleport - a respawn - is not a walk")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        cues.Walk(Me, start, true, 1.0f, out);
        cues.Walk(Me, start + glm::vec3(50.0f, 0.0f, 0.0f), true, 1.0f, out);
        CHECK(out.empty());
    }

    SUBCASE("two players keep their own strides")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        cues.Walk(Me, start, true, 1.0f, out);
        cues.Walk(Them, start, true, 1.0f, out);
        cues.Walk(Me, start + glm::vec3(SoundCues::Stride * 0.6f, 0.0f, 0.0f), true, 1.0f, out);
        cues.Walk(Them, start + glm::vec3(SoundCues::Stride * 0.6f, 0.0f, 0.0f), true, 1.0f, out);
        CHECK(out.empty());

        cues.Walk(Me, start + glm::vec3(SoundCues::Stride * 1.2f, 0.0f, 0.0f), true, 1.0f, out);
        REQUIRE(Count(out, Cue::Footstep) == 1);
        CHECK(out[0].Position == start + glm::vec3(SoundCues::Stride * 1.2f, 0.0f, 0.0f));
    }

    SUBCASE("each step varies, so a walk is not one sound repeated")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        for (int i = 0; i <= 4; ++i)
            cues.Walk(Me, start + glm::vec3(SoundCues::Stride * i, 0.0f, 0.0f), true, 1.0f, out);

        REQUIRE(out.size() == 4);
        CHECK(out[0].Variant != out[1].Variant);
    }
}

TEST_CASE("Joining a match does not replay what happened before")
{
    SoundCues cues;
    std::vector<CueToPlay> out;

    const std::deque<MatchClient::ShotReport> shots{ Shot(5, Them), Shot(6, Them) };
    const std::deque<MatchClient::ShownEdit> edits{
        Shown(9, { BlockEdit{ glm::ivec3(1, 1, 1), BlockId{ 0 } } }) };

    cues.SkipHistory(shots, edits);

    cues.Shots(shots, Me, Where, out);
    cues.Edits(edits, out);
    CHECK(out.empty());

    //What comes after is heard.
    std::deque<MatchClient::ShotReport> later = shots;
    later.push_back(Shot(7, Them));
    cues.Shots(later, Me, Where, out);
    CHECK(Count(out, Cue::Gunshot) == 1);
}

TEST_CASE("A remote player walks when their height holds steady")
{
    //Snapshots carry no grounded bit, so a remote player counts as walking
    //while the pose stays level - and not while it rises or falls.
    const glm::vec3 start{ 10.0f, 1.0f, 10.0f };

    SUBCASE("level travel steps")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        for (int i = 0; i <= 100; ++i)
            cues.WalkInferred(Them, start + glm::vec3(SoundCues::Stride * i / 100.0f, 0.0f, 0.0f),
                1.0f, out);

        CHECK(Count(out, Cue::Footstep) == 1);
    }

    SUBCASE("travel while rising or falling is silent")
    {
        SoundCues cues;
        std::vector<CueToPlay> out;

        for (int i = 0; i <= 100; ++i)
            cues.WalkInferred(Them,
                start + glm::vec3(SoundCues::Stride * i / 50.0f, 0.1f * i, 0.0f), 1.0f, out);

        CHECK(out.empty());
    }
}
