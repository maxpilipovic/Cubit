#include "cub.h"

#include "Cubit/Logger.h"

#include "Core/LogSink.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

void Logger::Init()
{
	Log("CLIENT", LogLevel::Info, "Logger initialized");
}

void Logger::Shutdown()
{
	Log("CLIENT", LogLevel::Info, "Logger shutdown");
}

bool Logger::OpenFile(const std::string& program, const std::string& directory)
{
	std::error_code error;
	std::filesystem::create_directories(directory, error);

	const std::time_t now = std::time(nullptr);
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &now);
	const int pid = _getpid();
#else
	localtime_r(&now, &local);
	const int pid = static_cast<int>(getpid());
#endif

	//The pid as well as the time, so two copies started in the same second - a
	//server and its clients from one script - never share a file.
	char name[128] = "";
	std::snprintf(name, sizeof(name), "%s-%04d%02d%02d-%02d%02d%02d-%d.log", program.c_str(),
		local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
		local.tm_hour, local.tm_min, local.tm_sec, pid);

	const std::string path = (std::filesystem::path(directory) / name).string();

	if (!LogSink::OpenFile(path))
	{
		Log("CLIENT", LogLevel::Warn, "Could not open log file " + path + "; logging to the console only");
		return false;
	}

	Log("CLIENT", LogLevel::Info, "Logging to " + path);
	return true;
}

void Logger::CloseFile()
{
	LogSink::CloseFile();
}

std::string Logger::FilePath()
{
	return LogSink::FilePath();
}

void Logger::Log(std::string_view channel, LogLevel level, std::string_view message)
{
	switch (level)
	{
		case LogLevel::Trace:    LogSink::Write(channel, "Trace", message); break;
		case LogLevel::Info:     LogSink::Write(channel, "Info", message); break;
		case LogLevel::Warn:     LogSink::Write(channel, "Warn", message); break;
		case LogLevel::Error:    LogSink::Write(channel, "Error", message); break;
		case LogLevel::Critical: LogSink::Write(channel, "Critical", message); break;
	}
}

void Logger::Trace(std::string_view message)
{
	Log("CLIENT", LogLevel::Trace, message);
}

void Logger::Info(std::string_view message)
{
	Log("CLIENT", LogLevel::Info, message);
}

void Logger::Warn(std::string_view message)
{
	Log("CLIENT", LogLevel::Warn, message);
}

void Logger::Error(std::string_view message)
{
	Log("CLIENT", LogLevel::Error, message);
}

void Logger::Critical(std::string_view message)
{
	Log("CLIENT", LogLevel::Critical, message);
}
