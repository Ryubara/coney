// SPDX-License-Identifier: GPL-3.0-or-later

// Trips the debug C runtime's own check the way a broken test once did (`front()` on an empty vector), after turning
// the dialogs off. ctest expects the report on the output and the process gone within its timeout: before
// platform/error_dialogs.h, this run sat on a "Debug Assertion Failed!" window until someone clicked it.

#include <cstdio>
#include <vector>

#include "platform/error_dialogs.h"

// Turns the dialogs off, then reads the front of an empty vector, which the debug runtime reports.
int main() {
    coney::platform::reportErrorsToConsole();
    std::vector<int> empty;
    std::fputs("reading the front of an empty vector\n", stderr);
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.UndefReturn): the debug runtime stops here, on purpose.
    return empty.front();
}
