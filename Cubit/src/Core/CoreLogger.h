#pragma once

#include "Cubit/Assert.h"

#include <string>
#include <string_view>

//Engine-only logging macros.
#define CB_CORE_TRACE(message) CoreLogger::Trace(message)
#define CB_CORE_INFO(message) CoreLogger::Info(message)
#define CB_CORE_WARN(message) CoreLogger::Warn(message)
#define CB_CORE_ERROR(message) CoreLogger::Error(message)
#define CB_CORE_CRITICAL(message) CoreLogger::Critical(message)

//Engine-only assertion. Reports on the core channel; see CB_ASSERT for when
//assertions are the right tool.
#ifdef CB_ENABLE_ASSERTS
	#define CB_CORE_ASSERT(condition, message)                                \
		do                                                                    \
		{                                                                     \
			if (!(condition))                                                 \
			{                                                                 \
				CoreLogger::Critical(                                         \
					std::string("Assertion failed: ") + (message) +           \
					" [" #condition "] at " __FILE__ ":" +                    \
					std::to_string(__LINE__));                                \
				CB_DEBUGBREAK();                                              \
			}                                                                 \
		} while (false)
#else
	#define CB_CORE_ASSERT(condition, message) do { } while (false)
#endif

class CoreLogger
{
public:
	//Prevents instances of the static core logger utility.
	CoreLogger() = delete;

	//Initializes the engine logging channel.
	static void Init();

	//Shuts down the engine logging channel.
	static void Shutdown();

	//Writes a trace-level engine message.
	static void Trace(std::string_view message);

	//Writes an informational engine message.
	static void Info(std::string_view message);

	//Writes a warning-level engine message.
	static void Warn(std::string_view message);

	//Writes an error-level engine message.
	static void Error(std::string_view message);

	//Writes a critical engine message.
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
