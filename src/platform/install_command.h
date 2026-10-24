// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/error.h"
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

/// Whether `install` is a finished install to play from: its `index/install.json` (written last by an install) and its
/// `game/ps2/` are there. Fails with ErrorCode::NotFound and a message saying how to finish it.
[[nodiscard]] std::expected<void, Error> checkInstallFolder(const std::filesystem::path& install);

/// The install folder this executable sits in (SDL's base folder), when it holds a finished install; nothing otherwise
/// (a build folder, a copy of the executable elsewhere).
[[nodiscard]] std::optional<std::string> installBesideExecutable();

} // namespace coney::platform
