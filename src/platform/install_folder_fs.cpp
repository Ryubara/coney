// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/install_folder_fs.h"

#include <format>
#include <fstream>
#include <system_error>
#include <utility>

#include "fileio/file_stream.h"

namespace coney::platform {

namespace {

// A file of the install folder being written, with standard C++ streams.
class HostOutputFile final : public install::OutputFile {
  public:
    HostOutputFile(std::ofstream file, std::filesystem::path path) : m_file(std::move(file)), m_path(std::move(path)) {}

    std::expected<void, Error> write(std::span<const std::byte> bytes) override {
        m_file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!m_file) {
            return fail(ErrorCode::Io, std::format("{}: cannot be written (is the drive full?)", m_path.string()));
        }
        return {};
    }

    std::expected<void, Error> close() override {
        m_file.close();
        if (m_file.fail()) {
            return fail(ErrorCode::Io, std::format("{}: cannot be written (is the drive full?)", m_path.string()));
        }
        return {};
    }

  private:
    std::ofstream m_file;
    std::filesystem::path m_path;
};

} // namespace

std::expected<std::unique_ptr<HostInstallFolder>, Error>
HostInstallFolder::openRoot(const std::filesystem::path& root) {
    std::error_code ec;
    if (std::filesystem::exists(root, ec) && !std::filesystem::is_directory(root, ec)) {
        return fail(ErrorCode::InvalidArgument, std::format("{}: a file, not a folder", root.string()));
    }
    std::filesystem::create_directories(root, ec);
    if (ec) {
        return fail(ErrorCode::Io, std::format("{}: cannot be made ({})", root.string(), ec.message()));
    }
    return std::unique_ptr<HostInstallFolder>(new HostInstallFolder(root));
}

std::expected<std::filesystem::path, Error> HostInstallFolder::resolve(std::string_view relative) const {
    const std::filesystem::path path{std::u8string(relative.begin(), relative.end())};
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return fail(ErrorCode::InvalidArgument, std::format("{}: not a path inside the install folder", relative));
    }
    for (const auto& part : path) {
        if (part == "..") {
            return fail(ErrorCode::InvalidArgument, std::format("{}: not a path inside the install folder", relative));
        }
    }
    return m_root / path;
}

std::expected<void, Error> HostInstallFolder::makeFolders(std::string_view relative) {
    auto path = resolve(relative);
    if (!path) {
        return std::unexpected(std::move(path.error()));
    }
    std::error_code ec;
    std::filesystem::create_directories(*path, ec);
    if (ec) {
        return fail(ErrorCode::Io, std::format("{}: cannot be made ({})", path->string(), ec.message()));
    }
    return {};
}

std::optional<std::uint64_t> HostInstallFolder::fileSize(std::string_view relative) const {
    auto path = resolve(relative);
    std::error_code ec;
    if (!path || !std::filesystem::is_regular_file(*path, ec)) {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(*path, ec);
    return ec ? std::nullopt : std::optional<std::uint64_t>(size);
}

std::expected<std::unique_ptr<install::OutputFile>, Error> HostInstallFolder::create(std::string_view relative) {
    auto path = resolve(relative);
    if (!path) {
        return std::unexpected(std::move(path.error()));
    }
    std::ofstream file(*path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(ErrorCode::Io, std::format("{}: cannot be created", path->string()));
    }
    return std::make_unique<HostOutputFile>(std::move(file), *path);
}

std::expected<std::unique_ptr<io::Stream>, Error> HostInstallFolder::open(std::string_view relative) const {
    auto path = resolve(relative);
    if (!path) {
        return std::unexpected(std::move(path.error()));
    }
    const auto size = fileSize(relative);
    if (!size) {
        return fail(ErrorCode::NotFound, std::format("{}: no such file", path->string()));
    }
    auto stream = io::FileStream::open(*path, 0, *size);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    return std::make_unique<io::FileStream>(std::move(*stream));
}

std::expected<void, Error> HostInstallFolder::rename(std::string_view from, std::string_view to) {
    auto source = resolve(from);
    auto target = resolve(to);
    if (!source || !target) {
        return std::unexpected(std::move(!source ? source.error() : target.error()));
    }
    // std::filesystem::rename replaces an existing file on every platform (MoveFileEx with replace on Windows).
    std::error_code ec;
    std::filesystem::rename(*source, *target, ec);
    if (ec) {
        return fail(ErrorCode::Io,
                    std::format("{}: cannot be renamed to {} ({})", source->string(), target->string(), ec.message()));
    }
    return {};
}

std::expected<void, Error> HostInstallFolder::remove(std::string_view relative) {
    auto path = resolve(relative);
    if (!path) {
        return std::unexpected(std::move(path.error()));
    }
    std::error_code ec;
    std::filesystem::remove(*path, ec);
    if (ec) {
        return fail(ErrorCode::Io, std::format("{}: cannot be removed ({})", path->string(), ec.message()));
    }
    return {};
}

std::optional<std::uint64_t> HostInstallFolder::freeBytes() const {
    std::error_code ec;
    const auto space = std::filesystem::space(m_root, ec);
    return ec ? std::nullopt : std::optional<std::uint64_t>(space.available);
}

} // namespace coney::platform
