// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/test_paths.h"

// The two folders reach the tests through this one file. They are absolute paths of the checkout being built, and a
// compile definition holding one made every test object's command line differ between worktrees, so the shared
// compiler cache missed all of them (docs/guides/building.md#compiler-cache); now only this file differs.
// tests/CMakeLists.txt sets them on this file alone.

namespace coney::test {

std::filesystem::path assetsDir() {
    return CONEY_ASSETS_DIR;
}

std::filesystem::path testSupportDir() {
    return CONEY_TEST_SUPPORT_DIR;
}

} // namespace coney::test
