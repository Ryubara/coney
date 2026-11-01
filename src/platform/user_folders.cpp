// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/user_folders.h"

#include <string>

#include <SDL3/SDL_filesystem.h>

namespace coney::platform {

std::optional<std::filesystem::path> userFolder(UserFolder which) {
    // SDL keeps the string for the life of the program (it is not ours to free) and gives null when there is none.
    const char* folder = SDL_GetUserFolder(which == UserFolder::Home ? SDL_FOLDER_HOME : SDL_FOLDER_DOCUMENTS);
    if (folder == nullptr || folder[0] == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(folder)));
}

} // namespace coney::platform
