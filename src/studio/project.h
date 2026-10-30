// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "studio/project_manifest.h"

// A Coney Studio project: a folder Studio creates, with a manifest at its root, which is the File system panel's root.
// Nothing outside the folder is reachable through it: every path Studio hands out or takes is written `project:/...`,
// with no absolute path and no `..` above the root (docs/guides/studio/editor-experience.md#project-coney). Coney's
// own; the original has no projects.

namespace coney::studio {

/// The scheme of a path inside the open project.
inline constexpr std::string_view kProjectScheme = "project:/";

/// The folders Studio makes in a new project, in order.
inline constexpr std::string_view kProjectFolders[] = {"levels", "textures", "scripts", ".studio"};

/// One entry of a project folder, as the File system panel lists it.
struct ProjectEntry {
    std::string name;        ///< The file or folder name.
    std::string projectPath; ///< Its `project:/` path.
    bool folder = false;
    std::uintmax_t size = 0; ///< Bytes, for a file.

    friend bool operator==(const ProjectEntry&, const ProjectEntry&) = default;
};

/// Turns `projectPath` into the path below the root it names, as forward-slash parts joined by '/', empty for the
/// root itself. Lexical only, touching no file. Fails with ErrorCode::InvalidArgument for a path that does not start
/// with `project:/`, holds a backslash, a drive or a NUL, or whose `..` parts climb above the root.
[[nodiscard]] std::expected<std::string, Error> normaliseProjectPath(std::string_view projectPath);

/// An open project: its root folder and manifest.
class Project {
  public:
    /// Makes a new project called `name` (validProjectName()) in the folder `root`, which must not exist yet or be
    /// empty: the folders of kProjectFolders, the manifest with `[play] level` set to `project:/levels/<level file>`,
    /// and `levelText` written there. Fails with ErrorCode::InvalidArgument for a bad name or a folder that holds
    /// files, and ErrorCode::Io when a folder or file cannot be written.
    [[nodiscard]] static std::expected<Project, Error> create(const std::filesystem::path& root, std::string_view name,
                                                              std::string_view levelFile, std::string_view levelText);
    /// Opens the project whose root is `root`. Fails with ErrorCode::NotFound when it holds no manifest, and as
    /// parseProjectManifest() does.
    [[nodiscard]] static std::expected<Project, Error> open(const std::filesystem::path& root);

    /// The root folder, absolute.
    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }
    /// The manifest as read or written.
    [[nodiscard]] const ProjectManifest& manifest() const { return m_manifest; }

    /// The file or folder `projectPath` names, inside the root. Fails as normaliseProjectPath() does, and with
    /// ErrorCode::InvalidArgument when an existing part of the path is a link leading out of the root.
    [[nodiscard]] std::expected<std::filesystem::path, Error> resolve(std::string_view projectPath) const;
    /// The `project:/` path of `path`, which may be relative to the root or absolute. Fails with
    /// ErrorCode::InvalidArgument for a path outside the root.
    [[nodiscard]] std::expected<std::string, Error> projectPathOf(const std::filesystem::path& path) const;
    /// The entries of the folder `projectPath`: folders first, then files, each group by name. Fails as resolve()
    /// does, and with ErrorCode::NotFound when it is not a folder.
    [[nodiscard]] std::expected<std::vector<ProjectEntry>, Error> list(std::string_view projectPath) const;

  private:
    // Only create() and open() make one.
    Project(std::filesystem::path root, ProjectManifest manifest)
        : m_root(std::move(root)), m_manifest(std::move(manifest)) {}

    std::filesystem::path m_root;
    ProjectManifest m_manifest;
};

} // namespace coney::studio
