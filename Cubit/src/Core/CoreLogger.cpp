#include "cub.h"

#include "Core/CoreLogger.h"

void CoreLogger::Init()
{
	std::cout << "[CORE] Logger initialized" << std::endl;
}

void CoreLogger::Shutdown()
{
	std::cout << "[CORE] Logger shutdown" << std::endl;
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

	//Flush every message. std::cout is fully buffered when redirected to a file
	//or pipe, so an unflushed crash loses exactly the output needed to diagnose it.
	std::cout << message << std::endl;
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
