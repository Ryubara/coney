// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace coney::platform {

/// Makes every failure end the process at once with a message on stderr, never a window waiting for a click.
///
/// On Windows the debug C runtime opens a modal "Debug Assertion Failed!" box for its own checks (a `front()` on an
/// empty vector, say), `abort()` opens another, and a crash brings up Windows Error Reporting. Nobody is there to
/// click in a test run, a CI job or an agent's scripted run, so the process would hang until a timeout. This sends the
/// runtime's reports to stderr and turns the boxes off. Elsewhere it does nothing: those systems print and abort.
///
/// Call it first thing in `main` (and before the tests run).
void reportErrorsToConsole();

} // namespace coney::platform
