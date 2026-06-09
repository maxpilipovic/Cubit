#include "cub.h"

#include "Cubit/Logger.h"

void Logger::Init()
{
	std::cout << "[CLIENT] Logger initialized\n";
}

void Logger::Shutdown()
{
	std::cout << "[CLIENT] Logger shutdown\n";
}

void Logger::Log(std::string_view channel, LogLevel level, std::string_view message)
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
