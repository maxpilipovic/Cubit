//Provides the entry point for the Cubit test executable.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "ConsoleProbe.h"
#include "CrashProbe.h"

#include <string_view>

int main(int argc, char** argv)
{
    //A child run of this same executable that crashes on purpose, for the crash
    //handler's tests - a crash cannot be tested from inside the process having
    //it. A normal run never passes this.
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg.starts_with(CrashProbeFlag))
            return RunCrashProbe(arg.substr(CrashProbeFlag.size()), argc, argv);

        //Children for the console QuickEdit test, which needs a console of its own.
        if (arg.starts_with(ConsoleProbeFlag))
            return RunConsoleProbe(arg.substr(ConsoleProbeFlag.size()));

        if (arg.starts_with(ConsoleLogProbeFlag))
            return RunConsoleLogProbe(arg.substr(ConsoleLogProbeFlag.size()));
    }

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
