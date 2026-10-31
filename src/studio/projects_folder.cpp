// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the editor's default projects folder; the original has no projects.
#include "studio/projects_folder.h"

#include <format>
#include <system_error>

namespace coney::studio {

std::optional<std::filesystem::path> defaultProjectsFolder(const UserFolders& folders) {
    // The documents folder, else home; an empty path is no folder (it would put the projects in the working folder).
    // Each check sits right before its read, which clang-tidy's optional-access check can follow (it cannot through a
    // helper).
    if (folders.documents && !folders.documents->empty()) {
        return *folders.documents / kStudioFolderName / kProjectsFolderName;
    }
    if (folders.home && !folders.home->empty()) {
        return *folders.home / kStudioFolderName / kProjectsFolderName;
    }
    return std::nullopt;
}

std::expected<void, Error> ensureFolderExists(const std::filesystem::path& folder) {
    std::error_code error;
    if (std::filesystem::is_directory(folder, error)) {
        return {};
    }
    std::filesystem::create_directories(folder, error);
    // create_directories reports a file in the way on some systems and returns false on others, so the folder is looked
    // at again.
    if (error || !std::filesystem::is_directory(folder, error)) {
        return fail(ErrorCode::Io, std::format("could not make the folder {}{}{}", folder.string(), error ? ": " : "",
                                               error ? error.message() : std::string()));
    }
    return {};
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

} // namespace coney::studio
