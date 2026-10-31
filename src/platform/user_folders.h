// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>

namespace coney::platform {

/// The user's folders Coney asks the operating system for.
enum class UserFolder {
    Home,      ///< The home folder.
    Documents, ///< `Documents` on Windows and macOS, the XDG documents directory on Linux.
};

/// The user's folder `which`, from SDL3 (`SDL_GetUserFolder`), or nothing when the system has none or cannot say. The
/// path is read as UTF-8, so a user name outside ASCII survives on Windows. Used by Coney Studio for its default
/// projects folder (docs/guides/studio/editor-experience.md#projects-folder); Coney's own, the original has no such
/// folders.
[[nodiscard]] std::optional<std::filesystem::path> userFolder(UserFolder which);

} // namespace coney::platform
