// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/error_dialogs.h"

#ifdef _WIN32
#include <array>
#include <crtdbg.h>
#include <cstdlib>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace coney::platform {

void reportErrorsToConsole() {
#ifdef _WIN32
    // The debug runtime's warnings, errors and assertions go to stderr instead of a dialog; the assertion then aborts
    // (these calls compile to nothing in a release runtime, which has no such reports).
    for (const int type : std::array<int, 3>{_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    // abort() neither shows its "abort() has been called" box nor asks Windows Error Reporting to collect a dump.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // A crash ends the process instead of opening the "has stopped working" window (kept with the existing mode bits).
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
}

} // namespace coney::platform
