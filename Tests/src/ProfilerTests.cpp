#include <doctest.h>

#include "Cubit/Profiler.h"

#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
    //Traces go to the temp directory and are removed by the tests that check
    //them, matching WorldSaveTests.cpp:165-170.
    std::string TempTracePath(const char* name)
    {
        return (std::filesystem::temp_directory_path() / name).string();
    }
}

TEST_CASE("A timed scope is recorded under its name")
{
    Profiler::BeginSession("test", TempTracePath("cubit_profiler_one.json"));
    {
        CB_PROFILE_SCOPE("Alpha");
    }
    const std::vector<ProfileResult> results = Profiler::EndSession();

    REQUIRE(results.size() == 1);
    CHECK(std::string(results[0].Name) == "Alpha");

    //Not "> 0": an empty scope completes in well under a microsecond, which
    //duration_cast truncates to exactly zero. The name and the count are what
    //carry this test.
    CHECK(results[0].DurationMicroseconds >= 0);
}

TEST_CASE("A nested scope is contained within the scope around it")
{
    Profiler::BeginSession("test", TempTracePath("cubit_profiler_nested.json"));
    {
        CB_PROFILE_SCOPE("Outer");
        {
            CB_PROFILE_SCOPE("Inner");
        }
    }
    const std::vector<ProfileResult> results = Profiler::EndSession();

    REQUIRE(results.size() == 2);

    //Found by name rather than by position. Both scopes are empty, so on a warm
    //CPU both start within the same microsecond and both last zero of them,
    //which no comparator can order — there is genuinely nothing left to order
    //them by. For the trace file that tie is harmless: two zero-duration events
    //at one timestamp draw identically either way. It breaks only a test that
    //asserts position, so this one asserts containment instead, which is the
    //property that actually matters.
    const ProfileResult* outer = nullptr;
    const ProfileResult* inner = nullptr;

    for (const ProfileResult& result : results)
    {
        if (std::string(result.Name) == "Outer")
            outer = &result;
        else if (std::string(result.Name) == "Inner")
            inner = &result;
    }

    REQUIRE(outer != nullptr);
    REQUIRE(inner != nullptr);

    //Nesting has no representation of its own — a trace viewer derives it from
    //containment alone — so containment is what has to be asserted.
    CHECK(inner->StartMicroseconds >= outer->StartMicroseconds);
    CHECK(inner->StartMicroseconds + inner->DurationMicroseconds <=
          outer->StartMicroseconds + outer->DurationMicroseconds);
}

TEST_CASE("A scope entered with no session active is dropped")
{
    {
        CB_PROFILE_SCOPE("Orphan");
    }

    Profiler::BeginSession("test", TempTracePath("cubit_profiler_orphan.json"));
    const std::vector<ProfileResult> results = Profiler::EndSession();

    //Recording is gated before the buffer is touched, so a stray macro in
    //engine code costs nothing when nobody is measuring.
    CHECK(results.empty());
}

TEST_CASE("Scopes from two threads merge with distinct thread ids")
{
    Profiler::BeginSession("test", TempTracePath("cubit_profiler_threads.json"));

    {
        CB_PROFILE_SCOPE("Main");
    }

    std::thread worker(
        []()
        {
            CB_PROFILE_SCOPE("Worker");
        });

    //Joined before EndSession, so the worker's buffer is gone by the time the
    //merge runs. That is the path ~ThreadBuffer's flush exists for, and the
    //samples it rescues — from short-lived workers — are the ones a threading
    //investigation most wants. The main thread's own scope covers the other
    //path, where the buffer is still registered at EndSession.
    worker.join();

    const std::vector<ProfileResult> results = Profiler::EndSession();

    REQUIRE(results.size() == 2);

    std::set<std::uint32_t> ids;
    for (const ProfileResult& result : results)
        ids.insert(result.ThreadId);

    CHECK(ids.size() == 2);
}
