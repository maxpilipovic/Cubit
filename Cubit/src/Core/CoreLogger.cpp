#include "cub.h"

#include "Core/CoreLogger.h"

void CoreLogger::Init()
{
	std::cout << "[CORE] Logger initialized\n";
}

void CoreLogger::Shutdown()
{
	std::cout << "[CORE] Logger shutdown\n";
}

void CoreLogger::Log(std::string_view channel, LogLevel level, std::string_view message)
{
	std::cout << '[' << channel << "] ";

	switch (level)
	{
	case LogLevel::Trace:
		std::cout << "[Trace] ";
		break;
	case LogLevel::Info:
		std::cout << "[Info] ";
		break;
	case LogLevel::Warn:
		std::cout << "[Warn] ";
		break;
	case LogLevel::Error:
		std::cout << "[Error] ";
		break;
	case LogLevel::Critical:
		std::cout << "[Critical] ";
		break;
	}

	std::cout << message << '\n';
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
