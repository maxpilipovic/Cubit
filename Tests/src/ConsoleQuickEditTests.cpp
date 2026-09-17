#include <doctest.h>

#include "ConsoleProbe.h"

#include "Cubit/Logger.h"

#include <filesystem>
#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace
{
    //The console this process is attached to, whatever its standard handles
    //have been redirected to.
    HANDLE OpenConsoleInput()
    {
        return CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }

    //Writes the console's input mode to `file` as a decimal number, or nothing
    //if there is no console to read.
    void WriteConsoleMode(const std::filesystem::path& file)
    {
        HANDLE input = OpenConsoleInput();
        DWORD mode = 0;
        const bool read = input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode);
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);

        std::ofstream out(file);
        if (read)
            out << mode;
    }

    //Runs `commandLine` and waits for it. False if it could not be started.
    bool RunAndWait(std::wstring commandLine, DWORD creationFlags)
    {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};

        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                creationFlags, nullptr, nullptr, &startup, &process))
            return false;

        WaitForSingleObject(process.hProcess, 60'000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    std::wstring SelfPath()
    {
        wchar_t self[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        return self;
    }

    std::wstring Widen(std::string_view text)
    {
        return std::filesystem::path(text).wstring();
    }

    //The mode a probe wrote, or -1 if it wrote none.
    long long ReadMode(const std::filesystem::path& file)
    {
        std::ifstream in(file);
        long long mode = -1;
        in >> mode;
        return mode;
    }
}

int RunConsoleProbe(std::string_view directory)
{
    //A console Windows opens for a program takes QuickEdit from the user's
    //settings. Set it on here rather than rely on them.
    HANDLE input = OpenConsoleInput();
    DWORD mode = 0;
    if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode))
        return 3;

    SetConsoleMode(input, mode | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);
    CloseHandle(input);

    const std::wstring child = L"\"" + SelfPath() + L"\" \"" + Widen(ConsoleLogProbeFlag)
        + Widen(directory) + L"\"";
    if (!RunAndWait(child, 0))
        return 4;

    WriteConsoleMode(std::filesystem::path(directory) / "after-exit.txt");
    return 0;
}

int RunConsoleLogProbe(std::string_view directory)
{
    Logger::Info("console probe");
    WriteConsoleMode(std::filesystem::path(directory) / "while-logging.txt");
    return 0;
}

TEST_CASE("Logging turns the console's QuickEdit off while the program runs, and back on at exit")
{
    //With QuickEdit on, one click in the console window starts a text selection,
    //and Windows holds every write to the console until the selection ends. The
    //Sandbox logs to the console, so a stray click froze the game on its next
    //line, and a frozen window cannot be clicked back into. Found on the A3 hand
    //check, 2026-09-17, and reproduced with the console's title reading "Select".
    const std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("cubit-console-probe-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const std::wstring probe = L"\"" + SelfPath() + L"\" \"" + Widen(ConsoleProbeFlag)
        + directory.wstring() + L"\"";
    REQUIRE(RunAndWait(probe, CREATE_NEW_CONSOLE));

    const long long whileLogging = ReadMode(directory / "while-logging.txt");
    const long long afterExit = ReadMode(directory / "after-exit.txt");
    std::filesystem::remove_all(directory);

    REQUIRE(whileLogging >= 0);
    REQUIRE(afterExit >= 0);

    CHECK((whileLogging & ENABLE_QUICK_EDIT_MODE) == 0);

    //The console can be a terminal the program was started from, which outlives
    //it: leave it as it was found.
    CHECK((afterExit & ENABLE_QUICK_EDIT_MODE) != 0);
}
