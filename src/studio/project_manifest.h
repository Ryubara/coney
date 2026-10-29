// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "core/error.h"

// A Coney Studio project's manifest, `coney-project.toml` at the project's root: its name and the level Play starts
// in. The fields are the Lua SDK design's (docs/guides/studio/lua-sdk.md#manifest); the prototype reads the
// `[project]` and `[play]` tables and writes a new manifest when it makes a project; it never rewrites an existing one,
// and unknown keys are dropped on reading, so a later save must carry them over. Coney's own; the original has no
// projects.

namespace coney::studio {

/// The manifest's file name, at the project's root.
inline constexpr std::string_view kManifestFile = "coney-project.toml";

/// What Studio reads from a manifest.
struct ProjectManifest {
    std::string name;      ///< `[project] name`: lower case, digits and '-'.
    std::string title;     ///< `[project] title`: what menus show.
    std::string version;   ///< `[project] version`.
    int sdk = 1;           ///< `[project] sdk`: the lowest SDK level the project needs.
    std::string playLevel; ///< `[play] level`: a `project:/` path to the level Play starts in.
    std::string spawn;     ///< `[play] spawn`: the spawn point the player starts at; empty for the level's first.

    friend bool operator==(const ProjectManifest&, const ProjectManifest&) = default;
};

/// Whether `name` is a valid project name: 1 to 64 characters of lower case letters, digits and '-', not starting
/// with '-'.
[[nodiscard]] bool validProjectName(std::string_view name);

/// Reads a manifest's text: `[table]` headers, `key = value` lines with a quoted string or a whole number as the value,
/// `#` comments. Keys Studio does not know are skipped, so a manifest a later Studio wrote still opens. Fails with
/// ErrorCode::Invalid and a message starting `line N:` for a line that is none of these, an unterminated string, or
/// a missing or invalid `[project] name`.
[[nodiscard]] std::expected<ProjectManifest, Error> parseProjectManifest(std::string_view text);

/// The manifest's text for `manifest`, which parseProjectManifest() reads back to the same values.
[[nodiscard]] std::string formatProjectManifest(const ProjectManifest& manifest);

} // namespace coney::studio
