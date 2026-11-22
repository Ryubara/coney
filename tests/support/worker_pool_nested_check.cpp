// SPDX-License-Identifier: GPL-3.0-or-later

// The child process of the worker pool's nested-loop test (tests/core/worker_pool_test.cpp): runs an outer loop of 40
// on a pool of four whose bodies each start a loop of the size given as the one argument (or none, for `none`), which
// the pool must refuse with CONEY_ASSERT, ending this process by its abort with the assertion's message. A broken
// invariant ends the process, so the test runs it apart and reads how it ended. Coney's own test support; the original
// has no counterpart.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/worker_pool.h"
#include "platform/error_dialogs.h"

// Runs the loops, and prints `done` when none was refused.
int main(int argc, char** argv) {
    // The abort of a refused loop must reach stderr, not open a window (platform/error_dialogs.h).
    coney::platform::reportErrorsToConsole();
    if (argc != 2) {
        std::fputs("usage: worker_pool_nested_check <nested loop size | none>\n", stderr);
        return 2;
    }
    const std::string argument = argv[1];
    const bool nests = argument != "none";
    const std::size_t nested = nests ? static_cast<std::size_t>(std::strtoull(argument.c_str(), nullptr, 10)) : 0;
    coney::WorkerPool pool(4);
    std::fputs("started\n", stdout);
    std::fflush(stdout);
    // An outer loop of 40 puts bodies on the workers as well as the caller, so a nested loop starts on either.
    pool.parallelFor(40, [&pool, nests, nested](std::size_t /*index*/) {
        if (nests) {
            pool.parallelFor(nested, [](std::size_t /*inner*/) {});
        }
    });
    std::fputs("done\n", stdout);
    return 0;
}
