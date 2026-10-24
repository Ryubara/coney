// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "core/error.h"
#include "install/install_folder.h"

namespace coney::platform {

/// The install folder on the host's drive: the folder the player picked, made when it does not exist yet.
/// Paths are relative to it with `/` between folders, as install::InstallFolder says; a path that climbs out of the
/// folder (`..`) or is absolute is refused, so nothing an install writes lands outside it.
class HostInstallFolder final : public install::InstallFolder {
  public:
    /// Opens (making it when needed) the folder `root`. Fails with ErrorCode::Io when it cannot be made, and
    /// ErrorCode::InvalidArgument when `root` is a file. Named apart from open() so that a string literal passed to
    /// open() is never ambiguous between a path and a relative name.
    [[nodiscard]] static std::expected<std::unique_ptr<HostInstallFolder>, Error>
    openRoot(const std::filesystem::path& root);

    // The InstallFolder interface; its doc comments give the contracts.
    [[nodiscard]] std::expected<void, Error> makeFolders(std::string_view relative) override;
    [[nodiscard]] std::optional<std::uint64_t> fileSize(std::string_view relative) const override;
    [[nodiscard]] std::expected<std::unique_ptr<install::OutputFile>, Error> create(std::string_view relative) override;
    [[nodiscard]] std::expected<std::unique_ptr<io::Stream>, Error> open(std::string_view relative) const override;
    [[nodiscard]] std::expected<void, Error> rename(std::string_view from, std::string_view to) override;
    [[nodiscard]] std::expected<void, Error> remove(std::string_view relative) override;
    [[nodiscard]] std::optional<std::uint64_t> freeBytes() const override;

    /// The folder itself.
    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

  private:
    explicit HostInstallFolder(std::filesystem::path root) : m_root(std::move(root)) {}

    // The host path of `relative`, or InvalidArgument for a path that is absolute or climbs out of the folder.
    [[nodiscard]] std::expected<std::filesystem::path, Error> resolve(std::string_view relative) const;

    std::filesystem::path m_root;
};

} // namespace coney::platform
