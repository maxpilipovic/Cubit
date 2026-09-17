#include <doctest.h>

#include "Cubit/Logger.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace
{
    //Everything written to std::cout while alive, and std::cout back as it was
    //afterwards.
    class CaptureConsole
    {
    public:
        CaptureConsole() : m_Previous(std::cout.rdbuf(m_Captured.rdbuf())) {}
        ~CaptureConsole() { std::cout.rdbuf(m_Previous); }

        std::string Text() const { return m_Captured.str(); }

    private:
        std::stringstream m_Captured;
        std::streambuf* m_Previous;
    };

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    std::filesystem::path FreshDirectory(const std::string& name)
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(directory);
        return directory;
    }

    int SecondsOfDay(int hours, int minutes, int seconds)
    {
        return hours * 3600 + minutes * 60 + seconds;
    }
}

TEST_CASE("Every log line starts with the wall-clock time")
{
    //A4 on the pre-game punch list. The 2026-09-14 loss investigation had to
    //timestamp lines from outside the process to line a client up against the
    //server - so the time is checked against the clock, not just for its shape.
    std::string text;
    {
        CaptureConsole console;
        CB_INFO("the time should come first");
        text = console.Text();
    }

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);

    const std::regex line(R"(^(\d\d):(\d\d):(\d\d)\.\d\d\d \[CLIENT\] \[Info\] the time should come first\n$)");
    std::smatch match;
    REQUIRE_MESSAGE(std::regex_match(text, match, line), "line was: " << text);

    const int logged = SecondsOfDay(std::stoi(match[1]), std::stoi(match[2]), std::stoi(match[3]));
    const int clock = SecondsOfDay(local.tm_hour, local.tm_min, local.tm_sec);

    //Either side of midnight, the two are a day apart and still close.
    const int apart = std::abs(clock - logged);
    CHECK(std::min(apart, 86400 - apart) <= 2);
}

TEST_CASE("An open log file gets every line from then on, and none after it closes")
{
    const std::filesystem::path directory = FreshDirectory("cubit-logger-test");

    {
        CaptureConsole console;

        CB_INFO("before the file opened");
        REQUIRE(Logger::OpenFile("unit", directory.string()));
        CB_WARN("while the file is open");
        Logger::CloseFile();
        CB_INFO("after the file closed");
    }

    //The directory was created, and the name says which program and when.
    REQUIRE(std::filesystem::exists(directory));

    std::filesystem::path logPath;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        logPath = entry.path();

    CHECK(std::regex_match(logPath.filename().string(), std::regex(R"(unit-\d{8}-\d{6}-\d+\.log)")));

    const std::string text = ReadFile(logPath);
    INFO("log file:\n" << text);

    CHECK(text.find("before the file opened") == std::string::npos);
    CHECK(std::regex_search(text, std::regex(R"(\d\d:\d\d:\d\d\.\d\d\d \[CLIENT\] \[Warn\] while the file is open\n)")));
    CHECK(text.find("after the file closed") == std::string::npos);
    CHECK(Logger::FilePath().empty());

    std::filesystem::remove_all(directory);
}

TEST_CASE("A log file that cannot be created leaves logging on the console")
{
    //A directory path that is already a file cannot hold a log.
    const std::filesystem::path blocker = FreshDirectory("cubit-logger-blocker");
    { std::ofstream file(blocker); file << "not a directory"; }

    std::string text;
    {
        CaptureConsole console;
        CHECK_FALSE(Logger::OpenFile("unit", blocker.string()));
        CB_INFO("still reaches the console");
        text = console.Text();
    }

    CHECK(Logger::FilePath().empty());
    CHECK(text.find("[Warn] Could not open log file") != std::string::npos);
    CHECK(text.find("still reaches the console") != std::string::npos);

    std::filesystem::remove_all(blocker);
}
