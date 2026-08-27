#include "cub.h"

#include "Cubit/Profiler.h"

#include <algorithm>
#include <atomic>

namespace
{
    std::atomic<bool> s_Active{ false };
    std::string s_SessionName;
    std::string s_OutputPath;
    std::chrono::steady_clock::time_point s_SessionStart;
    std::vector<ProfileResult> s_Results;

    std::int64_t Microseconds(
        std::chrono::steady_clock::time_point from,
        std::chrono::steady_clock::time_point to)
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(to - from)
            .count();
    }

    //Sorted so a viewer reads the file in order, and so the order is defined
    //rather than incidental. Descending duration breaks a tie on start time:
    //an enclosing scope starts no later and lasts at least as long as what it
    //encloses, so this puts the container first even when both were entered
    //within the same microsecond. std::sort is unstable, so without this a
    //nested pair could come back in either order.
    bool Earlier(const ProfileResult& a, const ProfileResult& b)
    {
        if (a.StartMicroseconds != b.StartMicroseconds)
            return a.StartMicroseconds < b.StartMicroseconds;

        return a.DurationMicroseconds > b.DurationMicroseconds;
    }
}

void Profiler::BeginSession(const char* name, const std::string& outputPath)
{
    s_SessionName = name;
    s_OutputPath = outputPath;
    s_Results.clear();
    s_SessionStart = std::chrono::steady_clock::now();
    s_Active.store(true, std::memory_order_relaxed);
}

std::vector<ProfileResult> Profiler::EndSession()
{
    if (!s_Active.exchange(false, std::memory_order_relaxed))
        return {};

    std::sort(s_Results.begin(), s_Results.end(), Earlier);

    return s_Results;
}

void Profiler::Record(const char* name,
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end)
{
    if (!s_Active.load(std::memory_order_relaxed))
        return;

    s_Results.push_back(ProfileResult{
        name,
        Microseconds(s_SessionStart, start),
        Microseconds(start, end),
        0 });
}

ProfileTimer::ProfileTimer(const char* name)
    : m_Name(name), m_Start(std::chrono::steady_clock::now())
{
}

ProfileTimer::~ProfileTimer()
{
    Profiler::Record(m_Name, m_Start, std::chrono::steady_clock::now());
}
