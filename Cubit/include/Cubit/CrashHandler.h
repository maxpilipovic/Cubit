#pragma once

#include "Cubit/Core.h"

#include <string>

//Makes a crash leave a record.
//
//Without this, an exception nobody catches or a native fault ends the process
//after whatever it last logged, and the question "what happened" has no answer.
//With it, both are logged as a critical line - the exception's type and message,
//or the fault's code, address and stack - and a minidump is written that opens
//in Visual Studio at the faulting line.
//
//Call once, at the top of main, before anything can throw. Native faults are
//caught on every thread; an uncaught exception only on the thread that called
//Install, because MSVC keeps std::terminate's handler per thread - which today
//is every thread the engine has. A debugger attached to the process sees faults
//first and this never runs, which is what debugging wants. A stack overflow is
//not reliably recorded: the handler has no stack left to run on.
class CB_API CrashHandler
{
public:
    CrashHandler() = delete;

    //`program` names the dump files, as `<program>-<yyyymmdd-hhmmss>-<pid>.dmp`, and
    //`dumpDirectory` is where they go, created on the first crash if missing.
    static void Install(const std::string& program, const std::string& dumpDirectory = "crashes");
};
