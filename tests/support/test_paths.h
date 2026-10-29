// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

namespace coney::test {

/// The checkout's `assets/` folder: Coney's own sandbox layouts, which the tests may read.
[[nodiscard]] std::filesystem::path assetsDir();

/// The checkout's `tests/support/` folder: the input scripts the disc tests play.
[[nodiscard]] std::filesystem::path testSupportDir();

/// The checkout's `python/tests/lua4/fixtures/` folder: Lua 4.0 sources of our own with the chunks a stock `luac`
/// made of them, which the decompiler's tests and the compiler's share.
[[nodiscard]] std::filesystem::path lua4FixturesDir();

} // namespace coney::test
