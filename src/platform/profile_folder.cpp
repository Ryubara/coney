// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/profile_folder.h"

#include <string>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

namespace coney::platform {

std::optional<std::filesystem::path> profileFolder(const Options& options) {
    if (options.profilesDir) {
        return std::filesystem::path(*options.profilesDir);
    }
    if (isTestMode(options)) {
        return std::nullopt;
    }
    char* folder = SDL_GetPrefPath("Coney", "Coney");
    if (folder == nullptr) {
        return std::nullopt;
    }
    // SDL gives UTF-8; read it as such so a user name outside ASCII survives on Windows.
    std::filesystem::path path =
        std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(folder))) / "profiles";
    SDL_free(folder);
    return path;
}

} // namespace coney::platform
