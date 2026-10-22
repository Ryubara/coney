// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/assert.h"

#include <cstdio>
#include <cstdlib>

#include "core/log.h"

namespace coney::detail {

void assertFailed(const char* expr, const char* file, int line) {
    // fprintf rather than iostreams or std::format: it allocates nothing, so it still works when the broken invariant
    // has left the heap in a bad state.
    std::fprintf(stderr, "Assertion failed: %s (%s:%d)\n", expr, file, line);
    // stderr is unbuffered by default, but flush anyway in case a platform layer has redirected it to a buffered file.
    std::fflush(stderr);
    // Then the run log's last 30 s, into the log file, for the bug report (docs/guides/run-log.md#ring). It may
    // allocate, so it comes after the line above, which then is out whatever happens here.
    logging::dumpOnFailure("assertion failed");
    std::abort();
}

} // namespace coney::detail
