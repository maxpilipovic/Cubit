#pragma once

#include "Cubit/Core.h"

#include <string_view>

// Client/game logging macros.
#define CB_TRACE(message) Logger::Trace(message)
#define CB_INFO(message) Logger::Info(message)
#define CB_WARN(message) Logger::Warn(message)
#define CB_ERROR(message) Logger::Error(message)
#define CB_CRITICAL(message) Logger::Critical(message)

class CB_API Logger
{
public:

	//Delete constructor
	Logger() = delete;

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
