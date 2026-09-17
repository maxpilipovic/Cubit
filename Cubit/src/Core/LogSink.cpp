#include "cub.h"

#include "Core/LogSink.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
#ifdef _WIN32
    //Turns QuickEdit off on the attached console for as long as this object
    //lives, and puts the console's mode back when it is destroyed.
    //
    //With QuickEdit on - how Windows opens a console for a program unless the
    //user has changed it - one click in the console window starts a text
    //selection, and every write to the console waits until the selection ends.
    //Every line goes to the console, so one stray click froze the Sandbox on its
    //next line, and a frozen window cannot be clicked back into (found on the A3
    //hand check, 2026-09-17). The console can be a terminal the program was
    //started from, which outlives it, hence the restore; a crash ends the process
    //with TerminateProcess and skips it.
    class ConsoleQuickEditOff
    {
    public:
        ConsoleQuickEditOff()
        {
            //CONIN$ rather than the standard input handle, which may have been
            //redirected while the console itself is still there.
            m_Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (m_Input == INVALID_HANDLE_VALUE)
                return;

            DWORD mode = 0;
            if (!GetConsoleMode(m_Input, &mode) || (mode & ENABLE_QUICK_EDIT_MODE) == 0)
                return;

            //Windows ignores the QuickEdit bit unless ENABLE_EXTENDED_FLAGS is set
            //in the same call.
            if (SetConsoleMode(m_Input, (mode & ~ENABLE_QUICK_EDIT_MODE) | ENABLE_EXTENDED_FLAGS))
                m_Restore = mode | ENABLE_EXTENDED_FLAGS;
        }

        ~ConsoleQuickEditOff()
        {
            if (m_Input == INVALID_HANDLE_VALUE)
                return;

            if (m_Restore != 0)
                SetConsoleMode(m_Input, m_Restore);

            CloseHandle(m_Input);
        }

        ConsoleQuickEditOff(const ConsoleQuickEditOff&) = delete;
        ConsoleQuickEditOff& operator=(const ConsoleQuickEditOff&) = delete;

    private:
        HANDLE m_Input = INVALID_HANDLE_VALUE;

        //The mode to put back, or 0 if nothing was changed.
        DWORD m_Restore = 0;
    };

    //Before the first line reaches the console.
    void KeepConsoleFromPausing()
    {
        static ConsoleQuickEditOff quickEditOff;
    }
#else
    void KeepConsoleFromPausing()
    {
    }
#endif

    //Recursive because the crash handler logs from whatever state the thread was
    //in - including, if a write itself faulted, halfway through one.
    std::recursive_mutex& Lock()
    {
        static std::recursive_mutex lock;
        return lock;
    }

    std::ofstream& File()
    {
        static std::ofstream file;
        return file;
    }

    std::string& Path()
    {
        static std::string path;
        return path;
    }

    //Wall-clock local time to the millisecond. Wall clock rather than time since
    //start, because what a timestamp is for here is lining up a client's log
    //against the server's, and two processes share only the wall clock.
    std::string Timestamp()
    {
        using namespace std::chrono;

        const system_clock::time_point now = system_clock::now();
        const std::time_t seconds = system_clock::to_time_t(now);
        const auto millis = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &seconds);
#else
        localtime_r(&seconds, &local);
#endif

        char text[16] = "";
        std::snprintf(text, sizeof(text), "%02d:%02d:%02d.%03d",
            local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(millis));
        return text;
    }
}

void LogSink::Write(std::string_view channel, std::string_view level, std::string_view message)
{
    std::string line = Timestamp();
    line += " [";
    line += channel;
    line += "] [";
    line += level;
    line += "] ";
    line += message;

    std::lock_guard guard(Lock());

    KeepConsoleFromPausing();
    std::cout << line << std::endl;

    if (File().is_open())
        File() << line << std::endl;
}

bool LogSink::OpenFile(const std::string& path)
{
    std::lock_guard guard(Lock());

    if (File().is_open())
        File().close();

    Path().clear();
    File().open(path, std::ios::out | std::ios::trunc);

    if (!File().is_open())
        return false;

    Path() = path;
    return true;
}

void LogSink::CloseFile()
{
    std::lock_guard guard(Lock());

    if (File().is_open())
        File().close();

    Path().clear();
}

std::string LogSink::FilePath()
{
    std::lock_guard guard(Lock());
    return Path();
}
