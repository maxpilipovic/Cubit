#pragma once

#include "Cubit/Core.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

//One timed scope, as it will be written out. Name is a string literal owned by
//the caller's translation unit, so recording copies no strings and allocates
//nothing per scope beyond the buffer's own amortised growth.
struct ProfileResult
{
    const char* Name = nullptr;
    std::int64_t StartMicroseconds = 0;     // relative to session start
    std::int64_t DurationMicroseconds = 0;
    std::uint32_t ThreadId = 0;             // small dense id, not an OS id
};

//Records how long named scopes took, and writes them where a trace viewer can
//draw them. Static, with all state owned inside the engine DLL: a macro has to
//be droppable into any translation unit without a reference to thread through
//the signatures around it, and header-owned state would give the DLL and each
//executable linking it a separate set of buffers.
class CB_API Profiler
{
public:
    Profiler() = delete;

    //Starts recording. Scopes entered before this are dropped rather than
    //buffered, so a capture contains what it says it contains.
    static void BeginSession(const char* name, const std::string& outputPath);

    //Stops recording, merges every thread's buffer, writes the trace file, and
    //returns the merged results.
    //
    //Returns the data as well as writing it so tests can assert on values
    //rather than scraping JSON, which is what keeps a JSON parser out of this
    //project.
    //
    //All recording threads must be quiesced -- joined, not merely idle --
    //before this is called: it touches every registered buffer, including
    //live ones, and the append side is deliberately lock-free.
    static std::vector<ProfileResult> EndSession();

private:
    friend class ProfileTimer;

    static void Record(const char* name,
        std::chrono::steady_clock::time_point start,
        std::chrono::steady_clock::time_point end);
};

//Times the scope it is declared in: constructed at entry, destroyed at exit.
//Declared through CB_PROFILE_SCOPE rather than by hand.
class CB_API ProfileTimer
{
public:
    explicit ProfileTimer(const char* name);
    ~ProfileTimer();

    ProfileTimer(const ProfileTimer&) = delete;
    ProfileTimer& operator=(const ProfileTimer&) = delete;

private:
    const char* m_Name;
    std::chrono::steady_clock::time_point m_Start;
};

//Two levels, so __LINE__ expands to its value before it is pasted rather than
//producing an identifier containing the characters "__LINE__".
#define CB_PROFILE_JOIN_INNER(a, b) a##b
#define CB_PROFILE_JOIN(a, b) CB_PROFILE_JOIN_INNER(a, b)

#ifdef CB_DIST
    //The shipping configuration pays nothing per scope: the macros expand to
    //nothing, so no scope is ever timed and Record is never called.
    //BeginSession/EndSession still exist and run in this configuration -- they
    //are not guarded on CB_DIST -- they simply have nothing to record. The API
    //still exists, so call sites compile in every configuration.
    #define CB_PROFILE_SCOPE(name)
    #define CB_PROFILE_FUNCTION()
#else
    #define CB_PROFILE_SCOPE(name) \
        const ProfileTimer CB_PROFILE_JOIN(cbProfileTimer, __LINE__)(name)

    //__FUNCSIG__ is MSVC. The project is CB_PLATFORM_WINDOWS-only, so it is
    //used directly rather than wrapped in a shim with no second case to serve.
    #define CB_PROFILE_FUNCTION() CB_PROFILE_SCOPE(__FUNCSIG__)
#endif
