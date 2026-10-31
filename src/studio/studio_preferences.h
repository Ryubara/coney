// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

// Coney Studio's preferences: what the user chose that belongs to them and not to a project, kept in one small text
// file in the per-user data folder beside the layouts (docs/guides/studio/editor-experience.md#theme-settings). Today
// that is the projects folder and the list of recent projects; the theme, the keymap and the rest join them as their
// build issues land. The text is a subset of TOML, as the project manifest is. Coney's own; the original has no
// editor.

namespace coney::studio {

/// The most recent projects the list keeps; opening one more drops the oldest.
inline constexpr std::size_t kMaxRecentProjects = 10;

/// One project in the recent list.
struct RecentProject {
    std::filesystem::path root; ///< The project's folder.
    std::string name;           ///< Its name when it was last opened, to show it while the folder is missing.

    friend bool operator==(const RecentProject&, const RecentProject&) = default;
};

/// What Studio keeps per user.
struct StudioPreferences {
    /// The projects folder the user chose; nothing means the default (defaultProjectsFolder()).
    std::optional<std::filesystem::path> projectsFolder;
    /// The recent projects, most recently opened first, at most kMaxRecentProjects.
    std::vector<RecentProject> recent;

    friend bool operator==(const StudioPreferences&, const StudioPreferences&) = default;
};

/// Puts `project` first in `preferences.recent`: an entry for the same folder is moved up (and renamed), a new one
/// pushed in, and the list cut to kMaxRecentProjects. Two folders are the same when their normal forms are equal, so
/// `a/b/../c` and `a/c` are one project.
void addRecentProject(StudioPreferences& preferences, RecentProject project);

/// Takes the project in `root` off the recent list. False when it was not on it. Compares as addRecentProject() does.
bool removeRecentProject(StudioPreferences& preferences, const std::filesystem::path& root);

/// Whether the folder `root` still holds a Studio project (its manifest file exists): a recent project that does not is
/// shown as missing.
[[nodiscard]] bool projectExists(const std::filesystem::path& root);

/// The text of `preferences`, which parsePreferences() reads back to the same values.
[[nodiscard]] std::string formatPreferences(const StudioPreferences& preferences);

/// Reads preferences text: `[projects] folder = "..."` and one `[[recent]]` table per project with `name` and `path`
/// strings, in the list's order; `#` comments. Keys it does not know are skipped, so a file a later Studio wrote still
/// reads; recent entries past kMaxRecentProjects and repeated folders are dropped. Fails with ErrorCode::Invalid and a
/// message starting `line N:` for a line that is not a table header or `key = value`, or an unterminated string.
[[nodiscard]] std::expected<StudioPreferences, Error> parsePreferences(std::string_view text);

/// Reads the preferences file `file`. A file that does not exist is not an error: the preferences are the defaults.
/// Fails with ErrorCode::Io when it cannot be read and as parsePreferences() does, naming the file.
[[nodiscard]] std::expected<StudioPreferences, Error> loadPreferences(const std::filesystem::path& file);

/// Writes `preferences` to `file`, making its folder first and replacing the file in one step, so a crash never leaves
/// half a file. Fails with ErrorCode::Io.
[[nodiscard]] std::expected<void, Error> savePreferences(const std::filesystem::path& file,
                                                         const StudioPreferences& preferences);

} // namespace coney::studio
