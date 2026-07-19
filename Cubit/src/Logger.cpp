#include "cub.h"

#include "Cubit/Logger.h"

void Logger::Init()
{
	std::cout << "[CLIENT] Logger initialized" << std::endl;
}

void Logger::Shutdown()
{
	std::cout << "[CLIENT] Logger shutdown" << std::endl;
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

	//Flush every message. std::cout is fully buffered when redirected to a file
	//or pipe, so an unflushed crash loses exactly the output needed to diagnose it.
	std::cout << message << std::endl;
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
