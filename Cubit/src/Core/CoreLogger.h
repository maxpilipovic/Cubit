#pragma once

#include <string_view>

// Engine-only logging macros.
#define CB_CORE_TRACE(message) CoreLogger::Trace(message)
#define CB_CORE_INFO(message) CoreLogger::Info(message)
#define CB_CORE_WARN(message) CoreLogger::Warn(message)
#define CB_CORE_ERROR(message) CoreLogger::Error(message)
#define CB_CORE_CRITICAL(message) CoreLogger::Critical(message)

class CoreLogger
{
public:
	CoreLogger() = delete;

	static void Init();
	static void Shutdown();

	static void Trace(std::string_view message);
	static void Info(std::string_view message);
	static void Warn(std::string_view message);
	static void Error(std::string_view message);
	static void Critical(std::string_view message);

private:
	enum class LogLevel
	{
		Trace,
		Info,
		Warn,
		Error,
		Critical
	};

	static void Log(std::string_view channel, LogLevel level, std::string_view message);
};
