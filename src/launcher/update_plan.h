// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/sha256.h"
#include "launcher/release_manifest.h"

namespace coney::launcher {

/// A program file the install holds now, as the launcher recorded it when it last installed one.
struct InstalledFile {
    std::string path;
    std::uint64_t size = 0;
    Sha256Digest sha256{};
};

/// What an update has to do, worked out before anything is downloaded.
struct UpdatePlan {
    /// Why the release cannot or need not be installed; Ready when it can.
    enum class Verdict : std::uint8_t {
        Ready,          ///< Files to fetch or remove.
        UpToDate,       ///< The release's files are what is installed.
        NoPlatform,     ///< The release has no build for this platform.
        LauncherTooOld, ///< The release needs a newer launcher first.
        NotNewer,       ///< The release is not newer than the installed version (no downgrade by update).
    };
    Verdict verdict = Verdict::UpToDate;
    std::vector<ReleaseFile> download; ///< Files missing or different, in the manifest's order.
    std::vector<std::string> remove;   ///< Installed program files the release no longer has.
    std::uint64_t downloadBytes = 0;
};

/// How the install folder's file system compares names: the platform layer, which knows the volume, says.
enum class PathCase : std::uint8_t {
    Exact,  ///< Case-sensitive (Linux, a case-sensitive macOS volume): `Coney.exe` and `coney.exe` are two files.
    Folded, ///< Case-insensitive (Windows, a default macOS volume): they are one file, compared as pathKey does.
};

/// Compares a release with what is installed: which files to download (the ones whose size or SHA-256 differs, or that
/// are missing), and which installed program files to remove. A release no newer than `installedVersion` is NotNewer
/// unless `repair` asks to restore the installed version's own files (the VERIFY tile). Only program files are ever
/// listed for removal: the game's files and the player's overrides are not the launcher's. Paths are matched as
/// `pathCase` says: folded, a file whose name changed only in case is the same file (neither downloaded again unchanged
/// nor removed after its new spelling is downloaded); exact, it is two files (the new spelling downloaded, the old one
/// removed).
[[nodiscard]] UpdatePlan planUpdate(const Release& release, std::string_view platform,
                                    std::span<const InstalledFile> installed, const Version& installedVersion,
                                    const Version& launcherVersion, PathCase pathCase, bool repair = false);

} // namespace coney::launcher
