// Prints where a program crashed: the exception, and the call stack with the
// file and line of each frame that has debug information.
//
// Without this, a check program that crashes on a machine with no debugger
// gives no hint of where. Call InstallCrashReport() at the start of main. It
// works on Windows only and does nothing elsewhere.

#pragma once

namespace examples
{

// Installs the report. Windows only; elsewhere it does nothing. The work is in
// CrashReport.cpp, which every example is built with, so windows.h and its
// macros stay out of the programs that include this.
void InstallCrashReport();

} // namespace examples
