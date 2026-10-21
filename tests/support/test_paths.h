// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

namespace coney::test {

/// The checkout's `assets/` folder: Coney's own sandbox layouts, which the tests may read.
[[nodiscard]] std::filesystem::path assetsDir();

/// The checkout's `tests/support/` folder: the input scripts the disc tests play.
[[nodiscard]] std::filesystem::path testSupportDir();

} // namespace coney::test
