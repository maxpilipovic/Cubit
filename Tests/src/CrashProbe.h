#pragma once

#include <string_view>

//`Tests.exe --crash-probe=<kind> --crash-dir=<directory>` installs the crash
//handler, writing dumps to <directory>, and then crashes the way <kind> names.
constexpr std::string_view CrashProbeFlag = "--crash-probe=";
constexpr std::string_view CrashDirFlag = "--crash-dir=";

//Never returns for a known kind. Returns 2 for an unknown one.
int RunCrashProbe(std::string_view kind, int argc, char** argv);
