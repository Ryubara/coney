// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/sha256.h"

namespace coney::launcher {

/// A release's version: `MAJOR.MINOR.PATCH`, optionally `-PRERELEASE` (`0.2.0`, `0.3.0-rc.1`). A pre-release sorts
/// before its release; two pre-releases sort by their text's dot-separated parts, numbers as numbers (Semantic
/// Versioning 2.0's order, without build metadata).
struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::string prerelease; ///< Empty for a release.

    /// The version as written: `0.3.0-rc.1`.
    [[nodiscard]] std::string text() const;
    /// Semantic Versioning's order.
    [[nodiscard]] std::strong_ordering operator<=>(const Version& other) const;
    [[nodiscard]] bool operator==(const Version& other) const = default;
};

/// Reads `MAJOR.MINOR.PATCH[-PRERELEASE]`; nullopt for anything else (a leading `v`, a missing part, a leading zero).
[[nodiscard]] std::optional<Version> parseVersion(std::string_view text);

/// One file of a release for one platform: where it goes in the install folder, its size and SHA-256, and the release
/// asset it is downloaded from.
struct ReleaseFile {
    std::string path; ///< Relative to the install folder, `/` between folders (`coney.exe`, `licences/SDL3.txt`).
    std::uint64_t size = 0;
    Sha256Digest sha256{};
    std::string asset; ///< The release asset's name (a plain file name).
};

/// A release, as its `launcher.json` describes it (docs/guides/launcher.md#manifest).
struct Release {
    Version version;
    std::string channel;            ///< `stable` or `preview`.
    Version minLauncher;            ///< The oldest launcher that can install it.
    std::vector<std::string> notes; ///< Short lines for the launcher's news panel.
    std::string changelogUrl;       ///< Where the full notes are.
    std::map<std::string, std::vector<ReleaseFile>, std::less<>> platforms; ///< `windows-x64`, `linux-x64`, ...
};

/// Parses a `launcher.json`. Fails with ErrorCode::Invalid, naming the field, for a format other than 1, a version
/// that does not parse, a file path that is absolute, climbs out (`..`), is empty or appears twice, a size that is not
/// a whole number, a SHA-256 that is not 64 hex digits, or an asset name with a folder in it.
[[nodiscard]] std::expected<Release, Error> parseRelease(std::string_view json);

} // namespace coney::launcher
