#pragma once

#include "Cubit/Net/MatchClient.h"
#include "Cubit/Voxel/MatchState.h"

#include <optional>

//Cubit's own game: turning the server's shot rulings into death announcements.
namespace CubitGame
{
    //Somebody died, and who killed them. Published on the gameplay event bus so
    //anything that wants to react to a death - a log line today, a kill feed
    //later - can do so without knowing that shooting is what caused it.
    struct PlayerDiedEvent
    {
        PlayerId Player = InvalidPlayer;
        PlayerId Killer = InvalidPlayer;
    };

    //Turns the server's shot rulings into at most one announcement per death.
    //
    //MatchClient::LastShot deliberately HOLDS the most recent ruling, so that a
    //caller can draw its impact marker for as many frames as it likes. A caller
    //that published on every frame it saw a kill would therefore announce the
    //same death over and over. This remembers which ruling it has spoken for.
    class DeathAnnouncer
    {
    public:
        //The event to publish for this ruling, or nothing: either nobody died,
        //or this death has already been announced.
        std::optional<PlayerDiedEvent> Observe(const MatchClient::ShotReport& shot)
        {
            if (!shot.Killed)
                return std::nullopt;

            //Compared rather than ordered, so a clock that starts over - a
            //reconnect, a new match - still announces its first death.
            if (m_AnnouncedTick.has_value() && *m_AnnouncedTick == shot.ReceivedAtTick)
                return std::nullopt;

            m_AnnouncedTick = shot.ReceivedAtTick;

            return PlayerDiedEvent{ shot.Victim, shot.Shooter };
        }

    private:
        //Which ruling has been announced, by the client tick it arrived on.
        //Empty until the first death, because tick zero is a real tick and a
        //kill that lands on it must not be mistaken for one already spoken for.
        std::optional<std::uint64_t> m_AnnouncedTick;
    };
}
