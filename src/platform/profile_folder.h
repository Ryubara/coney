// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>

#include "core/options.h"

namespace coney::platform {

/// The folder this run keeps the player profiles in (warriors/disk_profile_store.h): the one `--profiles` names, else
/// `profiles` in SDL's preference folder for Coney (`%APPDATA%\Coney\Coney\` on Windows, `~/.local/share/Coney/Coney/`
/// on Linux, `~/Library/Application Support/Coney/Coney/` on macOS). Nothing in test mode (isTestMode()) without
/// `--profiles`, so tests never read or write the player's own profiles, and nothing when SDL cannot give a folder:
/// the profiles then last the run.
///
/// Research: docs/research/save.md#coney
[[nodiscard]] std::optional<std::filesystem::path> profileFolder(const Options& options);

} // namespace coney::platform
