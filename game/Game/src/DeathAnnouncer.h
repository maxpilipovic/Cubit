#pragma once

#include "NewEvents.h"

#include "Cubit/Net/MatchClient.h"
#include "Cubit/Voxel/MatchState.h"

#include <deque>
#include <vector>

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

    //Turns the server's shot rulings into exactly one announcement per death.
    //
    //MatchClient::RecentShots HOLDS its rulings until newer ones push them out,
    //so a caller that announced every kill it saw each frame would announce the
    //same death over and over. This remembers which rulings it has read, by
    //serial - not by the tick they arrived on, which two rulings can share.
    class DeathAnnouncer
    {
    public:
        //The deaths among the rulings not yet read, oldest first.
        std::vector<PlayerDiedEvent> Observe(const std::deque<MatchClient::ShotReport>& recent)
        {
            std::vector<PlayerDiedEvent> deaths;

            for (const MatchClient::ShotReport* shot : m_Read.Take(recent))
                if (shot->Killed)
                    deaths.push_back(PlayerDiedEvent{ shot->Victim, shot->Shooter });

            return deaths;
        }

    private:
        NewEvents m_Read;
    };
}
