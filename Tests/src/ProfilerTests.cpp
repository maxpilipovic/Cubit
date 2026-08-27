#include <doctest.h>

#include "Cubit/Profiler.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
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
    const std::string path = TempTracePath("cubit_profiler_one.json");

    Profiler::BeginSession("test", path);
    {
        CB_PROFILE_SCOPE("Alpha");
    }
    const std::vector<ProfileResult> results = Profiler::EndSession();
    std::filesystem::remove(path);

    REQUIRE(results.size() == 1);
    CHECK(std::string(results[0].Name) == "Alpha");

    //Not "> 0": an empty scope completes in well under a microsecond, which
    //duration_cast truncates to exactly zero. The name and the count are what
    //carry this test.
    CHECK(results[0].DurationMicroseconds >= 0);
}

TEST_CASE("A nested scope is contained within the scope around it")
{
    const std::string path = TempTracePath("cubit_profiler_nested.json");

    Profiler::BeginSession("test", path);
    {
        CB_PROFILE_SCOPE("Outer");
        {
            CB_PROFILE_SCOPE("Inner");
        }
    }
    const std::vector<ProfileResult> results = Profiler::EndSession();
    std::filesystem::remove(path);

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
    const std::string path = TempTracePath("cubit_profiler_orphan.json");

    {
        CB_PROFILE_SCOPE("Orphan");
    }

    Profiler::BeginSession("test", path);
    const std::vector<ProfileResult> results = Profiler::EndSession();
    std::filesystem::remove(path);

    //This does NOT prove the s_Active gate in Profiler::Record actually ran:
    //BeginSession's own clear loop wipes every registered thread buffer before
    //EndSession ever reads it, so an orphaned sample would be erased either
    //way -- with the gate doing its job or with it deleted outright. What this
    //genuinely pins down is the outward guarantee: a macro fired outside any
    //session does not corrupt the capture that follows. The gate's own effect
    //is not observable through the public API and is covered by inspection
    //(see Profiler::Record's comment), not by this assertion -- the same
    //honesty the BlockEdit comparator test in docs/engine-roadmap.md needed
    //after an unfalsifiable assertion passed there once.
    CHECK(results.empty());
}

TEST_CASE("Scopes from two threads merge with distinct thread ids")
{
    const std::string path = TempTracePath("cubit_profiler_threads.json");

    Profiler::BeginSession("test", path);

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
    std::filesystem::remove(path);

    REQUIRE(results.size() == 2);

    std::set<std::uint32_t> ids;
    for (const ProfileResult& result : results)
        ids.insert(result.ThreadId);

    CHECK(ids.size() == 2);
}

namespace
{
    //Reads a whole file back. The suite deliberately contains no JSON parser:
    //values are asserted through EndSession's return, and the file is checked
    //only for the structure a viewer needs to open it.
    std::string ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path);
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
}

TEST_CASE("A session writes a trace holding the scopes it recorded")
{
    const std::string path = TempTracePath("cubit_profiler_written.json");

    Profiler::BeginSession("test", path);
    {
        CB_PROFILE_SCOPE("Written");
    }
    Profiler::EndSession();

    const std::string trace = ReadWholeFile(path);
    std::filesystem::remove(path);

    CHECK(trace.find("\"traceEvents\"") != std::string::npos);
    CHECK(trace.find("\"name\":\"Written\"") != std::string::npos);
    CHECK(trace.find("\"ph\":\"X\"") != std::string::npos);
    CHECK(trace.rfind("]}") != std::string::npos);
}

TEST_CASE("A session with no scopes writes a valid empty trace")
{
    const std::string path = TempTracePath("cubit_profiler_empty.json");

    Profiler::BeginSession("test", path);
    Profiler::EndSession();

    const std::string trace = ReadWholeFile(path);
    std::filesystem::remove(path);

    //An empty capture must still open in a viewer rather than producing a
    //truncated file that reads as a crash.
    CHECK(trace.find("\"traceEvents\"") != std::string::npos);
    CHECK(trace.find("\"name\"") == std::string::npos);
    CHECK(trace.rfind("]}") != std::string::npos);
}

TEST_CASE("A name containing a quote is escaped rather than breaking the file")
{
    const std::string path = TempTracePath("cubit_profiler_escaped.json");

    Profiler::BeginSession("test", path);
    {
        //__FUNCSIG__ is unsanitised compiler output pasted into JSON, so the
        //two characters JSON forbids are escaped rather than trusted.
        CB_PROFILE_SCOPE("Quo\"te");
    }
    Profiler::EndSession();

    const std::string trace = ReadWholeFile(path);
    std::filesystem::remove(path);

    CHECK(trace.find("Quo\\\"te") != std::string::npos);
}

TEST_CASE("Beginning a session while one is active writes the first one out")
{
    const std::string first = TempTracePath("cubit_profiler_first.json");
    const std::string second = TempTracePath("cubit_profiler_second.json");

    Profiler::BeginSession("first", first);
    {
        CB_PROFILE_SCOPE("Early");
    }

    //Nothing is lost: the abandoned session is ended properly rather than
    //discarded, and the warning names the mistake.
    Profiler::BeginSession("second", second);
    {
        CB_PROFILE_SCOPE("Late");
    }
    const std::vector<ProfileResult> results = Profiler::EndSession();

    const std::string firstTrace = ReadWholeFile(first);
    std::filesystem::remove(first);
    std::filesystem::remove(second);

    CHECK(firstTrace.find("\"name\":\"Early\"") != std::string::npos);

    REQUIRE(results.size() == 1);
    CHECK(std::string(results[0].Name) == "Late");
}
