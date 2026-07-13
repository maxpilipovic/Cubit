#pragma once

#include "Cubit/Core.h"

#include <string_view>

//Client/game logging macros.
#define CB_TRACE(message) Logger::Trace(message)
#define CB_INFO(message) Logger::Info(message)
#define CB_WARN(message) Logger::Warn(message)
#define CB_ERROR(message) Logger::Error(message)
#define CB_CRITICAL(message) Logger::Critical(message)

class CB_API Logger
{
public:

	//Prevents instances of the static logger utility.
	Logger() = delete;

	//Initializes the client logging channel.
	static void Init();

	//Shuts down the client logging channel.
	static void Shutdown();

	//Writes a trace-level client message.
	static void Trace(std::string_view message);

	//Writes an informational client message.
	static void Info(std::string_view message);

	//Writes a warning-level client message.
	static void Warn(std::string_view message);

	//Writes an error-level client message.
	static void Error(std::string_view message);

	//Writes a critical client message.
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

	//Formats and writes a message for the requested channel and level.
	static void Log(std::string_view channel, LogLevel level, std::string_view message);
};
