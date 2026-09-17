#include <doctest.h>

#include "CrashProbe.h"

#include "Cubit/CrashHandler.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace
{
    //Out of line, so the stack a crash reports has a frame of this file's own to
    //look for rather than one the optimiser folded into main.
    [[noreturn]] __declspec(noinline) void ThrowFromProbe()
    {
        throw std::runtime_error("the probe threw this on purpose");
    }

    [[noreturn]] __declspec(noinline) void ThrowNonStandardFromProbe()
    {
        throw 42;
    }

    __declspec(noinline) void WriteThroughNullFromProbe()
    {
        int* volatile nowhere = nullptr;
        *nowhere = 42;
    }
}

int RunCrashProbe(std::string_view kind, int argc, char** argv)
{
    std::string directory = "crashes";
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg.starts_with(CrashDirFlag))
            directory = std::string(arg.substr(CrashDirFlag.size()));
    }

    CrashHandler::Install("probe", directory);

    if (kind == "exception")
        ThrowFromProbe();

    if (kind == "non-standard-exception")
        ThrowNonStandardFromProbe();

    if (kind == "access-violation")
        WriteThroughNullFromProbe();

    return 2;
}

namespace
{
    struct ProbeRun
    {
        int ExitCode = 0;
        std::string Log;
        std::vector<std::filesystem::path> Dumps;
    };

    //Runs this executable as a child that crashes the way `kind` names, and
    //collects what the crash left behind: its exit code, everything it printed,
    //and the dump files in a directory of its own.
    ProbeRun RunProbe(const std::string& kind)
    {
        char self[MAX_PATH] = "";
        GetModuleFileNameA(nullptr, self, MAX_PATH);

        const std::filesystem::path directory = std::filesystem::temp_directory_path()
            / ("cubit-crash-probe-" + std::to_string(GetCurrentProcessId()) + "-" + kind);
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);

        const std::filesystem::path logPath = directory / "log.txt";
        const std::filesystem::path dumpDirectory = directory / "dumps";

        //The outer quotes are for cmd.exe, which std::system runs this through
        //and which strips one pair from a command that starts with a quote.
        const std::string command = "\"\"" + std::string(self) + "\" " + std::string(CrashProbeFlag) + kind
            + " \"" + std::string(CrashDirFlag) + dumpDirectory.string() + "\" > \""
            + logPath.string() + "\" 2>&1\"";

        ProbeRun run;
        run.ExitCode = std::system(command.c_str());

        std::ifstream log(logPath);
        std::stringstream text;
        text << log.rdbuf();
        run.Log = text.str();
        log.close();

        if (std::filesystem::exists(dumpDirectory))
        {
            for (const auto& entry : std::filesystem::directory_iterator(dumpDirectory))
            {
                if (entry.path().extension() == ".dmp" && entry.file_size() > 0)
                    run.Dumps.push_back(entry.path());
            }
        }

        std::filesystem::remove_all(directory);
        return run;
    }

    bool Contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }
}

TEST_CASE("An exception nobody catches is logged with its type and message, and leaves a dump")
{
    //A2 on the pre-game punch list: before the crash handler, an exception out
    //of a layer ended the process after whatever it had last logged.
    const ProbeRun run = RunProbe("exception");
    INFO("child output:\n" << run.Log);

    CHECK(run.ExitCode != 0);
    CHECK(Contains(run.Log, "Terminating: uncaught exception class std::runtime_error: the probe threw this on purpose"));

    //The stack is walked from inside the terminate handler, and a C++ exception
    //nobody caught has not unwound, so the throwing function is still on it.
    CHECK(Contains(run.Log, "ThrowFromProbe"));

    CHECK(Contains(run.Log, "Wrote crash dump"));
    CHECK(run.Dumps.size() == 1);
}

TEST_CASE("An uncaught exception not derived from std::exception is still logged")
{
    const ProbeRun run = RunProbe("non-standard-exception");
    INFO("child output:\n" << run.Log);

    CHECK(run.ExitCode != 0);
    CHECK(Contains(run.Log, "Terminating: uncaught exception of a type not derived from std::exception"));
    CHECK(run.Dumps.size() == 1);
}

TEST_CASE("A native crash is logged with where it happened and a stack, and leaves a dump")
{
    const ProbeRun run = RunProbe("access-violation");
    INFO("child output:\n" << run.Log);

    //Exits with the fault's own code, the same as with no handler installed.
    CHECK(static_cast<std::uint32_t>(run.ExitCode) == 0xC0000005u);

    CHECK(Contains(run.Log, "Crashed: access violation writing 0x0 at Tests.exe+0x"));
    CHECK(Contains(run.Log, "Stack:"));
    CHECK(Contains(run.Log, "WriteThroughNullFromProbe"));
    CHECK(Contains(run.Log, "CrashHandlerTests.cpp:"));
    CHECK(Contains(run.Log, "Wrote crash dump"));
    CHECK(run.Dumps.size() == 1);
}
