// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/file_stream.h"

#include <format>
#include <ios>
#include <system_error>
#include <utility>

namespace coney::io {

std::expected<FileStream, Error> FileStream::open(const std::filesystem::path& path, std::uint64_t offset,
                                                  std::uint64_t length) {
    std::error_code ec;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return fail(ErrorCode::NotFound, std::format("{}: cannot be read ({})", path.string(), ec.message()));
    }
    // Written so that a huge offset cannot wrap around: offset + length <= fileSize.
    if (offset > fileSize || length > fileSize - offset) {
        return fail(ErrorCode::Truncated, std::format("{}: needed bytes {} to {} but the file is {} bytes long",
                                                      path.string(), offset, offset + length, fileSize));
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("{}: cannot be opened", path.string()));
    }
    return FileStream(std::move(file), offset, length);
}

std::expected<void, Error> FileStream::read(std::span<std::byte> destination) {
    if (destination.size() > m_length - m_position) {
        return fail(ErrorCode::Truncated, std::format("needed {} bytes at offset {} but only {} remain",
                                                      destination.size(), m_position, m_length - m_position));
    }
    if (destination.empty()) {
        return {};
    }
    // Clear the error flags a short read at the end of the file may have set: while they are set, seekg does nothing.
    m_file.clear();
    m_file.seekg(static_cast<std::streamoff>(m_offset + m_position));
    m_file.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    if (!m_file || static_cast<std::size_t>(m_file.gcount()) != destination.size()) {
        return fail(ErrorCode::Io,
                    std::format("read of {} bytes at offset {} failed", destination.size(), m_offset + m_position));
    }
    m_position += destination.size();
    return {};
}

std::expected<void, Error> FileStream::seek(std::uint64_t position) {
    if (position > m_length) {
        return fail(ErrorCode::Truncated,
                    std::format("cannot seek to offset {}: the stream is {} bytes long", position, m_length));
    }
    m_position = position;
    return {};
}

} // namespace coney::io
