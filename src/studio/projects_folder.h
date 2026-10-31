// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.h"

// Where Coney Studio keeps a user's projects by default: `Coney Studio/Projects` inside the user's documents folder, or
// inside the home folder when the platform has no documents folder
// (docs/guides/studio/editor-experience.md#projects-folder). The folders themselves come from the platform layer (SDL3,
// src/platform/user_folders.h); this is the rule that turns them into the projects folder, so it is tested over made-up
// folders with no operating system. Coney's own; the original has no projects.

namespace coney::studio {

/// The folder of Studio's own inside the documents (or home) folder.
inline constexpr std::string_view kStudioFolderName = "Coney Studio";
/// The projects folder's name inside kStudioFolderName.
inline constexpr std::string_view kProjectsFolderName = "Projects";

/// The user's folders the platform knows, either of which may be missing.
struct UserFolders {
    std::optional<std::filesystem::path>
        documents; ///< `Documents` on Windows and macOS, the XDG documents directory on Linux.
    std::optional<std::filesystem::path> home; ///< The home folder.
};

/// The default projects folder: `Coney Studio/Projects` inside `folders.documents`, else inside `folders.home`; nothing
/// when the platform reported neither. Lexical: touches no file.
[[nodiscard]] std::optional<std::filesystem::path> defaultProjectsFolder(const UserFolders& folders);

/// Makes `folder` and the folders above it when they do not exist (the first use of the default). Fails with
/// ErrorCode::Io when it cannot, or when `folder` exists as a file.
[[nodiscard]] std::expected<void, Error> ensureFolderExists(const std::filesystem::path& folder);

/// `path` as UTF-8 text, which is what Dear ImGui's text fields and SDL hold: the narrow string of a path is the
/// system's code page on Windows and loses letters outside it.
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);

/// The path the UTF-8 `text` names; the reverse of pathToUtf8().
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text);

} // namespace coney::studio
