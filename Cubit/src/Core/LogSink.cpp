#include "cub.h"

#include "Core/LogSink.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace
{
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
