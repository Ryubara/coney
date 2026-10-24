// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <filesystem>
#include <span>
#include <vector>

#include "core/options.h"
#include "install/known_discs.h"

namespace coney::platform {

/// Runs `coney --install DISC --data DIR [--verify]`: opens the disc and the install folder, installs
/// (install::install()) and prints one line per file and every tenth of the way to `out`, then a summary of counts,
/// bytes and the disc's digest; never a file's contents. Returns the process's exit code: 0 when the install
/// finished, 1 when it was refused or failed (the message says why, and how to go on).
///
/// docs/guides/running.md#install
[[nodiscard]] int runInstallCommand(const Options& options, std::span<const install::KnownDisc> known,
                                    const std::vector<std::filesystem::path>& programFiles, std::FILE* out);

/// The program files `coney --install` copies into the install folder `installRoot`: this executable, found in SDL's
/// base folder by the name it was started with. None when it cannot be found, or when the install folder is the
/// executable's own folder (a file cannot be copied over itself while it runs).
[[nodiscard]] std::vector<std::filesystem::path> installProgramFiles(const std::filesystem::path& installRoot,
                                                                     const char* argv0);

} // namespace coney::platform
