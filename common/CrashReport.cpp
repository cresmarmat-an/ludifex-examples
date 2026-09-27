// See CrashReport.h.

#include "CrashReport.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <dbghelp.h>

#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace examples
{

LONG WINAPI ReportCrash(EXCEPTION_POINTERS* info)
{
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    std::fprintf(stderr, "\nCRASHED: exception 0x%08lx at %p, on thread %lu\n",
                 static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
                 info->ExceptionRecord->ExceptionAddress, static_cast<unsigned long>(GetCurrentThreadId()));

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int depth = 0; depth < 48; ++depth)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0)
        {
            break;
        }

        const DWORD64 address = frame.AddrPC.Offset;

        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        const char* name = SymFromAddr(process, address, &displacement, symbol) ? symbol->Name : "?";

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
        {
            std::fprintf(stderr, "  %s  %s:%lu\n", name, line.FileName, static_cast<unsigned long>(line.LineNumber));
        }
        else
        {
            std::fprintf(stderr, "  %s  (0x%llx)\n", name, static_cast<unsigned long long>(address));
        }
    }

    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashReport()
{
    SetUnhandledExceptionFilter(ReportCrash);
}

} // namespace examples

#else

namespace examples
{
void InstallCrashReport()
{
}
} // namespace examples

#endif
