#pragma once

#include <cstdint>
#include <deque>
#include <vector>

//Cubit's own game: reading MatchClient's numbered histories one event at a time.
namespace CubitGame
{
    //Remembers how far through a history of serial-numbered events it has read,
    //so each event is taken exactly once however many frames the history holds
    //it for and however many arrive together.
    //
    //Serials count up from 1, so zero means "nothing read yet" without a
    //separate flag.
    class NewEvents
    {
    public:
        //The events newer than the last call, oldest first.
        template <typename Event>
        std::vector<const Event*> Take(const std::deque<Event>& history)
        {
            std::vector<const Event*> fresh;

            //Serials only count up within one client, so a history whose
            //newest is older than what was read belongs to a new client - a
            //reconnect - and all of it is news.
            if (!history.empty() && history.back().Serial < m_Last)
                m_Last = 0;

            for (const Event& event : history)
                if (event.Serial > m_Last)
                    fresh.push_back(&event);

            if (!history.empty() && history.back().Serial > m_Last)
                m_Last = history.back().Serial;

            return fresh;
        }

        //Marks everything in the history as already read, without taking it.
        //For joining a match mid-way: its recent past is not news.
        template <typename Event>
        void SkipTo(const std::deque<Event>& history)
        {
            if (!history.empty() && history.back().Serial > m_Last)
                m_Last = history.back().Serial;
        }

    private:
        std::uint64_t m_Last = 0;
    };
}
