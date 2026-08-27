#include "cub.h"

#include "Cubit/Profiler.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace
{
    std::atomic<bool> s_Active{ false };
    std::string s_SessionName;
    std::string s_OutputPath;
    std::chrono::steady_clock::time_point s_SessionStart;

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
        if (a.ThreadId != b.ThreadId)
            return a.ThreadId < b.ThreadId;

        if (a.StartMicroseconds != b.StartMicroseconds)
            return a.StartMicroseconds < b.StartMicroseconds;

        return a.DurationMicroseconds > b.DurationMicroseconds;
    }

    //Each thread appends to its own buffer with no lock, so recording never
    //contends and never distorts the timings it is taking. A mutex on the hot
    //path would make the profiler's own contention part of the measurement
    //under exactly the parallel workload it exists to measure.
    struct ThreadBuffer;

    std::mutex s_Mutex;
    std::vector<ThreadBuffer*> s_Registered;
    std::vector<ProfileResult> s_Merged;   // flushed by threads that have exited
    std::uint32_t s_NextThreadId = 0;

    struct ThreadBuffer
    {
        //Registers on this thread's first record, and nowhere else, so the
        //mutex is taken twice per thread rather than twice per scope.
        //
        //The id is a dense counter rather than a hash of std::thread::id: a
        //viewer draws one lane per id, and 0, 1, 2 are readable where
        //ten-digit hashes in arbitrary order are not.
        ThreadBuffer()
        {
            const std::lock_guard<std::mutex> lock(s_Mutex);
            ThreadId = s_NextThreadId++;
            s_Registered.push_back(this);
        }

        //Flushes rather than merely unregistering. A worker that exits before
        //EndSession would otherwise take its samples with it.
        ~ThreadBuffer()
        {
            const std::lock_guard<std::mutex> lock(s_Mutex);

            s_Merged.insert(s_Merged.end(), Results.begin(), Results.end());
            s_Registered.erase(
                std::remove(s_Registered.begin(), s_Registered.end(), this),
                s_Registered.end());
        }

        std::vector<ProfileResult> Results;
        std::uint32_t ThreadId = 0;
    };

    thread_local ThreadBuffer t_Buffer;
}

void Profiler::BeginSession(const char* name, const std::string& outputPath)
{
    s_SessionName = name;
    s_OutputPath = outputPath;

    {
        const std::lock_guard<std::mutex> lock(s_Mutex);

        s_Merged.clear();
        for (ThreadBuffer* buffer : s_Registered)
            buffer->Results.clear();
    }

    s_SessionStart = std::chrono::steady_clock::now();
    s_Active.store(true, std::memory_order_relaxed);
}

std::vector<ProfileResult> Profiler::EndSession()
{
    if (!s_Active.exchange(false, std::memory_order_relaxed))
        return {};

    std::vector<ProfileResult> merged;
    {
        const std::lock_guard<std::mutex> lock(s_Mutex);

        merged.swap(s_Merged);

        //Cleared but left registered: a second session on the same threads
        //costs no re-registration and starts empty.
        for (ThreadBuffer* buffer : s_Registered)
        {
            merged.insert(merged.end(),
                buffer->Results.begin(), buffer->Results.end());
            buffer->Results.clear();
        }
    }

    std::sort(merged.begin(), merged.end(), Earlier);

    return merged;
}

void Profiler::Record(const char* name,
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end)
{
    //Checked before t_Buffer is named, so no session means no thread ever
    //registers and no buffer is ever constructed.
    if (!s_Active.load(std::memory_order_relaxed))
        return;

    t_Buffer.Results.push_back(ProfileResult{
        name,
        Microseconds(s_SessionStart, start),
        Microseconds(start, end),
        t_Buffer.ThreadId });
}

ProfileTimer::ProfileTimer(const char* name)
    : m_Name(name), m_Start(std::chrono::steady_clock::now())
{
}

ProfileTimer::~ProfileTimer()
{
    Profiler::Record(m_Name, m_Start, std::chrono::steady_clock::now());
}
