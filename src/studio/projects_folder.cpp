// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the editor's default projects folder; the original has no projects.
#include "studio/projects_folder.h"

namespace coney::studio {

std::optional<std::filesystem::path> defaultProjectsFolder(const UserFolders& folders) {
    // Stand-in for the real rule, which the commit after this one makes.
    static_cast<void>(folders);
    return std::nullopt;
}

std::expected<void, Error> ensureFolderExists(const std::filesystem::path& folder) {
    static_cast<void>(folder);
    return {};
}

} // namespace coney::studio
