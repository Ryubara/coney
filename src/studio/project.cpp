// SPDX-License-Identifier: GPL-3.0-or-later
#include "studio/project.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <vector>

namespace coney::studio {

namespace {

// Writes `text` to `path`, replacing it; fails with ErrorCode::Io.
std::expected<void, Error> writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out.good()) {
        return fail(ErrorCode::Io, std::format("could not write {}", path.string()));
    }
    return {};
}

// Whether `path` lies inside `root` (or is it), both already absolute and normal.
bool inside(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

// A refusal of `projectPath` for leaving the project.
std::unexpected<Error> outside(std::string_view projectPath) {
    return fail(ErrorCode::InvalidArgument,
                std::format("{} is outside the project: nothing above its root is reachable", projectPath));
}

} // namespace

std::expected<std::string, Error> normaliseProjectPath(std::string_view projectPath) {
    if (!projectPath.starts_with(kProjectScheme)) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("{} is not a project path (they start with {})", projectPath, kProjectScheme));
    }
    const std::string_view rest = projectPath.substr(kProjectScheme.size());
    if (rest.find_first_of(std::string_view("\\:\0", 3)) != std::string_view::npos) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("{}: a project path uses '/' only and names no drive", projectPath));
    }
    // Walk the parts: "" and "." stay, ".." takes one back and may not pass the root.
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= rest.size()) {
        const auto slash = rest.find('/', start);
        const auto part = rest.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (part == "..") {
            if (parts.empty()) {
                return outside(projectPath);
            }
            parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    std::string joined;
    for (const auto part : parts) {
        if (!joined.empty()) {
            joined += '/';
        }
        joined += part;
    }
    return joined;
}

std::expected<Project, Error> Project::create(const std::filesystem::path& root, std::string_view name,
                                              std::string_view levelFile, std::string_view levelText) {
    // 1. The checks, before anything is written.
    if (!validProjectName(name)) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("\"{}\" is not a project name: lower case letters, digits and '-'", name));
    }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(root, error).lexically_normal();
    if (std::filesystem::exists(absolute, error) && !std::filesystem::is_empty(absolute, error)) {
        return fail(
            ErrorCode::InvalidArgument,
            std::format("{} already holds files: a new project needs an empty or new folder", absolute.string()));
    }
    // 2. The folders, the manifest and the first level.
    for (const auto folder : kProjectFolders) {
        std::filesystem::create_directories(absolute / folder, error);
        if (error) {
            return fail(ErrorCode::Io,
                        std::format("could not make {}: {}", (absolute / folder).string(), error.message()));
        }
    }
    const ProjectManifest manifest{.name = std::string(name),
                                   .title = std::string(name),
                                   .version = "0.1.0",
                                   .sdk = 1,
                                   .playLevel = std::format("{}levels/{}", kProjectScheme, levelFile),
                                   .spawn = ""};
    if (auto written = writeText(absolute / kManifestFile, formatProjectManifest(manifest)); !written) {
        return std::unexpected(std::move(written.error()));
    }
    if (auto written = writeText(absolute / "levels" / levelFile, levelText); !written) {
        return std::unexpected(std::move(written.error()));
    }
    return Project(absolute, manifest);
}

std::expected<Project, Error> Project::open(const std::filesystem::path& root) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(root, error).lexically_normal();
    std::ifstream in(absolute / kManifestFile, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::NotFound,
                    std::format("{} is not a Coney Studio project: it has no {}", absolute.string(), kManifestFile));
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto manifest = parseProjectManifest(text.str());
    if (!manifest) {
        return fail(manifest.error().code,
                    std::format("{}: {}", (absolute / kManifestFile).string(), manifest.error().message));
    }
    // Keep the root without a trailing separator, so paths made from it compare equal.
    auto clean = absolute.has_filename() ? absolute : absolute.parent_path();
    return Project(std::move(clean), std::move(*manifest));
}

std::expected<std::filesystem::path, Error> Project::resolve(std::string_view projectPath) const {
    const auto relative = normaliseProjectPath(projectPath);
    if (!relative) {
        return std::unexpected(relative.error());
    }
    auto path = relative->empty() ? m_root : (m_root / std::filesystem::path(*relative)).lexically_normal();
    // A link inside the project may lead out of it: what exists is followed and must stay inside the real root.
    std::error_code rootError;
    std::error_code pathError;
    const auto realRoot = std::filesystem::weakly_canonical(m_root, rootError);
    const auto realPath = std::filesystem::weakly_canonical(path, pathError);
    if (const std::error_code error = rootError ? rootError : pathError) {
        // The real location cannot be told, so the path is refused rather than trusted.
        return fail(ErrorCode::Io, std::format("could not look up {}: {}", projectPath, error.message()));
    }
    if (!inside(realRoot, realPath)) {
        return outside(projectPath);
    }
    return path;
}

std::expected<std::string, Error> Project::projectPathOf(const std::filesystem::path& path) const {
    const auto absolute = (path.is_absolute() ? path : m_root / path).lexically_normal();
    if (!inside(m_root, absolute)) {
        return outside(absolute.string());
    }
    const auto relative = absolute.lexically_relative(m_root).generic_string();
    return std::string(kProjectScheme) + (relative == "." ? std::string() : relative);
}

std::expected<std::vector<ProjectEntry>, Error> Project::list(std::string_view projectPath) const {
    const auto folder = resolve(projectPath);
    if (!folder) {
        return std::unexpected(folder.error());
    }
    std::error_code error;
    if (!std::filesystem::is_directory(*folder, error)) {
        return fail(ErrorCode::NotFound, std::format("{} is not a folder", projectPath));
    }
    std::vector<ProjectEntry> entries;
    for (const auto& item : std::filesystem::directory_iterator(*folder, error)) {
        auto path = projectPathOf(item.path());
        if (!path) {
            continue; // a link pointing away is not listed
        }
        const bool isFolder = item.is_directory(error);
        entries.push_back(ProjectEntry{.name = item.path().filename().string(),
                                       .projectPath = std::move(*path),
                                       .folder = isFolder,
                                       .size = isFolder ? 0 : item.file_size(error)});
    }
    // Folders first, then files, each by name in byte order, so the order never depends on the file system.
    std::ranges::sort(entries, [](const ProjectEntry& a, const ProjectEntry& b) {
        return a.folder != b.folder ? a.folder : a.name < b.name;
    });
    return entries;
}

} // namespace coney::studio
