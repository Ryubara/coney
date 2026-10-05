// SPDX-License-Identifier: GPL-3.0-or-later

// Catch2 supplies the tests' main, so the error dialogs are turned off from a static initialiser instead: a failed
// runtime check in a test must fail that run on stderr, never leave it waiting on a window (platform/error_dialogs.h).

#include "platform/error_dialogs.h"

namespace {

// Runs before main, and so before any test.
const bool kDialogsOff = [] {
    coney::platform::reportErrorsToConsole();
    return true;
}();

} // namespace
