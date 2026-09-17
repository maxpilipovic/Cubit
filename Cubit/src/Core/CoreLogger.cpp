#include "cub.h"

#include "Core/CoreLogger.h"

#include "Core/LogSink.h"

void CoreLogger::Init()
{
	Log("CORE", LogLevel::Info, "Logger initialized");
}

void CoreLogger::Shutdown()
{
	Log("CORE", LogLevel::Info, "Logger shutdown");
}

void CoreLogger::Log(std::string_view channel, LogLevel level, std::string_view message)
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

void CoreLogger::Trace(std::string_view message)
{
	Log("CORE", LogLevel::Trace, message);
}

void CoreLogger::Info(std::string_view message)
{
	Log("CORE", LogLevel::Info, message);
}

void CoreLogger::Warn(std::string_view message)
{
	Log("CORE", LogLevel::Warn, message);
}

void CoreLogger::Error(std::string_view message)
{
	Log("CORE", LogLevel::Error, message);
}

void CoreLogger::Critical(std::string_view message)
{
	Log("CORE", LogLevel::Critical, message);
}
