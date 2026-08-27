#include "cub.h"

#include "Cubit/Profiler.h"

#include "Core/CoreLogger.h"

#include <algorithm>
#include <atomic>
#include <fstream>
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

    //Escapes the two characters a JSON string may not contain.
    std::string Escape(const char* text)
    {
        std::string escaped;
        for (const char* c = text; *c != '\0'; ++c)
        {
            if (*c == '"' || *c == '\\')
                escaped.push_back('\\');

            escaped.push_back(*c);
        }
        return escaped;
    }

    //Chrome trace format: an array of complete events, microseconds throughout.
    //Nesting needs no representation — events on one tid nest by containment,
    //so the viewer derives the flame graph from ts and dur alone.
    void WriteTrace(const std::vector<ProfileResult>& results,
        const std::string& path)
    {
        std::ofstream file(path);
        if (!file)
        {
            CB_CORE_ERROR("Profiler: cannot open trace file: " + path);
            return;
        }

        file << "{\"otherData\":{},\"traceEvents\":[";

        for (std::size_t i = 0; i < results.size(); ++i)
        {
            const ProfileResult& result = results[i];

            if (i != 0)
                file << ',';

            file << "\n{\"cat\":\"function\""
                 << ",\"dur\":" << result.DurationMicroseconds
                 << ",\"name\":\"" << Escape(result.Name) << '"'
                 << ",\"ph\":\"X\""
                 << ",\"pid\":0"
                 << ",\"tid\":" << result.ThreadId
                 << ",\"ts\":" << result.StartMicroseconds
                 << '}';
        }

        file << "\n]}";
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
        //A namespace-scope thread_local's initialization order is
        //implementation-defined; MSVC constructs one eagerly for every thread
        //that exists — the main thread's before main() runs, a worker's at
        //thread creation — regardless of whether a session is active. So
        //every thread registers exactly once over its lifetime, taking
        //s_Mutex twice in total (register here, flush below) rather than
        //twice per scope; it is the s_Active gate in Record, not lazy
        //construction, that keeps an idle session from being appended to.
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

//All recording threads must be quiesced -- joined, not merely idle -- before
//this is called. It clears every registered buffer, including ones belonging
//to threads that are still running, with no lock held on the append side: a
//live thread's concurrent push_back would race a reallocating clear(), which
//is undefined behaviour rather than a merely stale read.
void Profiler::BeginSession(const char* name, const std::string& outputPath)
{
    //Ends the abandoned session rather than discarding it or throwing: a
    //misuse with an honest answer gets the answer, the way ApplyBlockEdit
    //returns nullopt rather than throwing on a bad coordinate.
    if (s_Active.load(std::memory_order_relaxed))
    {
        CB_CORE_WARN("Profiler: session '" + s_SessionName +
            "' was still recording; ending it before starting '" +
            std::string(name) + "'");

        EndSession();
    }

    s_SessionName = name;
    s_OutputPath = outputPath;

    {
        const std::lock_guard<std::mutex> lock(s_Mutex);

        s_Merged.clear();
        for (ThreadBuffer* buffer : s_Registered)
            buffer->Results.clear();
    }

    s_SessionStart = std::chrono::steady_clock::now();

    //Release pairs with Record's acquire load: it publishes both the
    //s_SessionStart write above and the buffer clears above it, so a thread
    //that observes s_Active == true also observes a session start it can
    //correctly measure against. Relaxed would give atomicity on the flag
    //alone with no such guarantee, leaving StartMicroseconds free to read a
    //stale s_SessionStart.
    s_Active.store(true, std::memory_order_release);
}

//Same precondition as BeginSession: every recording thread must already be
//joined. This walks and clears every registered buffer, live or not, with no
//lock on the append side, so a buffer still being written to by a running
//thread is a data race, not just a chance of missing its last few samples.
std::vector<ProfileResult> Profiler::EndSession()
{
    if (!s_Active.exchange(false, std::memory_order_relaxed))
        return {};

    std::vector<ProfileResult> merged;
    {
        const std::lock_guard<std::mutex> lock(s_Mutex);

        merged.swap(s_Merged);

        //Cleared but left registered: a thread that is joined and later
        //replaced by a new one on the same session, or a still-live thread
        //profiled across two sessions after this call returns, re-uses its
        //slot for free. It is not an invitation to keep a worker alive across
        //BeginSession/EndSession itself -- that thread's buffer is being
        //cleared here while it may still be appending to it.
        for (ThreadBuffer* buffer : s_Registered)
        {
            merged.insert(merged.end(),
                buffer->Results.begin(), buffer->Results.end());
            buffer->Results.clear();
        }
    }

    std::sort(merged.begin(), merged.end(), Earlier);

    WriteTrace(merged, s_OutputPath);

    return merged;
}

void Profiler::Record(const char* name,
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end)
{
    //t_Buffer is already constructed by the time this runs — MSVC registers
    //every thread's buffer eagerly, session or no session — so this gate is
    //what actually keeps an idle session free of appends, not the buffer's
    //construction. Acquire pairs with BeginSession's release store to
    //s_Active, so a thread that observes true here also observes the
    //s_SessionStart write that preceded it.
    if (!s_Active.load(std::memory_order_acquire))
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
