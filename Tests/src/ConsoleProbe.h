#pragma once

#include <string_view>

//`Tests.exe --console-probe=<directory>` runs in a console of its own with
//QuickEdit on, starts a `--console-log-probe` child sharing that console, and
//once the child has exited writes the console's mode to <directory>/after-exit.txt.
constexpr std::string_view ConsoleProbeFlag = "--console-probe=";

//`Tests.exe --console-log-probe=<directory>` logs one line, then writes the
//console's mode to <directory>/while-logging.txt.
constexpr std::string_view ConsoleLogProbeFlag = "--console-log-probe=";

int RunConsoleProbe(std::string_view directory);
int RunConsoleLogProbe(std::string_view directory);
