#include "cub.h"

#include "Cubit/CrashHandler.h"

#include "Core/CoreLogger.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <typeinfo>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#endif

namespace
{
    //Copied into fixed buffers at install time, so a crash never has to allocate
    //to find out where its dump goes.
    char s_Program[64] = "cubit";
    char s_DumpDirectory[260] = "crashes";

    //Set by whichever handler runs first. A second fault while handling the
    //first - a heap too damaged to log into, say - ends the process at once
    //rather than recursing.
    std::atomic_flag s_Handling = ATOMIC_FLAG_INIT;

    void CopyInto(char* buffer, std::size_t size, const std::string& text)
    {
        std::snprintf(buffer, size, "%s", text.c_str());
    }

#ifdef _WIN32
    //What SetUnhandledExceptionFilter returned: the C runtime's own filter,
    //which is the thing that turns a C++ exception nobody caught into a call to
    //std::terminate.
    LPTOP_LEVEL_EXCEPTION_FILTER s_PreviousFilter = nullptr;

    //MSVC raises every C++ exception as this SEH code.
    constexpr DWORD MsvcCppExceptionCode = 0xE06D7363;

    [[noreturn]] void EndProcess(UINT exitCode)
    {
        TerminateProcess(GetCurrentProcess(), exitCode);

        //TerminateProcess does not return for the calling process; this only
        //satisfies [[noreturn]].
        std::abort();
    }

    std::string ModuleAndOffset(DWORD64 address)
    {
        char text[MAX_PATH + 32] = "?";

        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address), &module))
        {
            char path[MAX_PATH] = "";
            GetModuleFileNameA(module, path, MAX_PATH);

            const char* name = path;
            for (const char* c = path; *c != '\0'; ++c)
                if (*c == '\\' || *c == '/')
                    name = c + 1;

            std::snprintf(text, sizeof(text), "%s+0x%llx", name,
                static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
        }
        else
        {
            std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(address));
        }

        return text;
    }

    //One line per frame, innermost first: module+offset, and the function and
    //source line wherever a PDB is found for it.
    void LogStack(CONTEXT context)
    {
#if defined(_M_X64)
        const HANDLE process = GetCurrentProcess();
        const HANDLE thread = GetCurrentThread();

        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(process, nullptr, TRUE);

        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        CB_CORE_CRITICAL("Stack:");

        constexpr int MaxFrames = 48;
        for (int depth = 0; depth < MaxFrames; ++depth)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                    nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;

            const DWORD64 address = frame.AddrPC.Offset;
            if (address == 0)
                break;

            std::string line = "  #" + std::to_string(depth) + " " + ModuleAndOffset(address);

            char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;

            DWORD64 displacement = 0;
            if (SymFromAddr(process, address, &displacement, symbol))
            {
                line += std::string(" ") + symbol->Name;

                IMAGEHLP_LINE64 source{};
                source.SizeOfStruct = sizeof(source);
                DWORD lineDisplacement = 0;
                if (SymGetLineFromAddr64(process, address, &lineDisplacement, &source))
                    line += std::string(" (") + source.FileName + ":" + std::to_string(source.LineNumber) + ")";
            }

            CB_CORE_CRITICAL(line);
        }

        SymCleanup(process);
#else
        (void)context;
#endif
    }

    //`info` is null when there is no fault to describe - std::terminate - and
    //the dump then records the calling thread as it stands.
    void WriteDump(EXCEPTION_POINTERS* info)
    {
        CreateDirectoryA(s_DumpDirectory, nullptr);

        SYSTEMTIME now{};
        GetLocalTime(&now);

        char path[MAX_PATH * 2] = "";
        std::snprintf(path, sizeof(path), "%s\\%s-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
            s_DumpDirectory, s_Program, now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());

        const HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE)
        {
            CB_CORE_CRITICAL(std::string("Could not create crash dump ") + path);
            return;
        }

        MINIDUMP_EXCEPTION_INFORMATION exception{};
        exception.ThreadId = GetCurrentThreadId();
        exception.ExceptionPointers = info;
        exception.ClientPointers = FALSE;

        //Stacks and the memory they point at: enough to see locals at every
        //frame, and megabytes rather than the whole address space - a 512-wide
        //world alone is hundreds of megabytes.
        const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);

        const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
            info != nullptr ? &exception : nullptr, nullptr, nullptr);

        CloseHandle(file);

        if (written)
            CB_CORE_CRITICAL(std::string("Wrote crash dump ") + path);
        else
            CB_CORE_CRITICAL(std::string("Could not write crash dump ") + path);
    }

    const char* DescribeCode(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:         return "access violation";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "array bounds exceeded";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "datatype misalignment";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "float divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR:            return "in-page error";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "integer divide by zero";
        case EXCEPTION_PRIV_INSTRUCTION:         return "privileged instruction";
        case EXCEPTION_STACK_OVERFLOW:           return "stack overflow";
        case 0xC0000374:                         return "heap corruption";   //STATUS_HEAP_CORRUPTION
        default:                                 return "native exception";
        }
    }

    LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
    {
        const EXCEPTION_RECORD& record = *info->ExceptionRecord;

        //A C++ exception nobody caught. The runtime's filter turns it into
        //std::terminate with the exception still current, which is what lets
        //OnTerminate name it - so it goes there rather than being described here
        //as an anonymous SEH code.
        if (record.ExceptionCode == MsvcCppExceptionCode && s_PreviousFilter != nullptr)
            return s_PreviousFilter(info);

        if (s_Handling.test_and_set())
            EndProcess(record.ExceptionCode);

        char text[256] = "";
        const std::string where = ModuleAndOffset(reinterpret_cast<DWORD64>(record.ExceptionAddress));

        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        {
            const char* operation = record.ExceptionInformation[0] == 0 ? "reading"
                : record.ExceptionInformation[0] == 1 ? "writing" : "executing";

            std::snprintf(text, sizeof(text), "Crashed: access violation %s 0x%llx at %s", operation,
                static_cast<unsigned long long>(record.ExceptionInformation[1]), where.c_str());
        }
        else
        {
            std::snprintf(text, sizeof(text), "Crashed: %s (0x%08lX) at %s",
                DescribeCode(record.ExceptionCode), record.ExceptionCode, where.c_str());
        }

        CB_CORE_CRITICAL(text);
        LogStack(*info->ContextRecord);
        WriteDump(info);

        //Ends the process with the exception code as its exit code, the same as
        //with no handler installed.
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif

    [[noreturn]] void OnTerminate()
    {
        if (s_Handling.test_and_set())
        {
#ifdef _WIN32
            EndProcess(3);
#else
            std::_Exit(3);
#endif
        }

        std::string what = "std::terminate was called with no exception in flight";

        if (const std::exception_ptr current = std::current_exception())
        {
            try
            {
                std::rethrow_exception(current);
            }
            catch (const std::exception& error)
            {
                what = std::string("uncaught exception ") + typeid(error).name() + ": " + error.what();
            }
            catch (...)
            {
                what = "uncaught exception of a type not derived from std::exception";
            }
        }

        CB_CORE_CRITICAL("Terminating: " + what);

#ifdef _WIN32
        CONTEXT context{};
        RtlCaptureContext(&context);
        LogStack(context);
        WriteDump(nullptr);

        //Not std::abort: in a Debug build that raises a modal "abort() has been
        //called" dialog, which leaves a process nobody is watching - a server, a
        //scripted run - hanging instead of exiting.
        EndProcess(3);
#else
        std::_Exit(3);
#endif
    }
}

void CrashHandler::Install(const std::string& program, const std::string& dumpDirectory)
{
    CopyInto(s_Program, sizeof(s_Program), program);
    CopyInto(s_DumpDirectory, sizeof(s_DumpDirectory), dumpDirectory);

    std::set_terminate(OnTerminate);

#ifdef _WIN32
    s_PreviousFilter = SetUnhandledExceptionFilter(OnUnhandledException);
#endif
}
