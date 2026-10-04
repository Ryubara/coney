// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// Programmer errors only: prints the expression, file and line to stderr, then aborts. Active in every build type,
/// because a broken invariant in a release build is still a bug we want to hear about.
///
/// Never use it for bad input (a damaged disc, a wrong command line): those return a coney::Error instead.
#define CONEY_ASSERT(expr) ((expr) ? static_cast<void>(0) : ::coney::detail::assertFailed(#expr, __FILE__, __LINE__))

namespace coney::detail {

/// The failure path of CONEY_ASSERT, kept out of line so the macro stays small at every call site.
/// Prints `Assertion failed: <expr> (<file>:<line>)` to stderr and aborts; never returns.
[[noreturn]] void assertFailed(const char* expr, const char* file, int line);

} // namespace coney::detail
