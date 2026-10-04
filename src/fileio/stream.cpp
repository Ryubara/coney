// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/stream.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "fileio/reader.h"

namespace coney::io {

namespace {

std::unexpected<Error> pastEnd(std::uint64_t wanted, std::uint64_t position, std::uint64_t remaining) {
    return fail(ErrorCode::Truncated,
                std::format("needed {} bytes at offset {} but only {} remain", wanted, position, remaining));
}

std::unexpected<Error> seekPastEnd(std::uint64_t position, std::uint64_t size) {
    return fail(ErrorCode::Truncated,
                std::format("cannot seek to offset {}: the stream is {} bytes long", position, size));
}

} // namespace

std::expected<void, Error> Stream::skip(std::uint64_t count) {
    if (count > remaining()) {
        return pastEnd(count, tell(), remaining());
    }
    return seek(tell() + count);
}

std::expected<std::uint32_t, Error> Stream::readU32Le() {
    std::array<std::byte, 4> bytes{};
    if (auto done = read(bytes); !done) {
        return std::unexpected(std::move(done.error()));
    }
    return loadU32Le(bytes);
}

std::expected<void, Error> MemoryStream::read(std::span<std::byte> destination) {
    if (destination.size() > m_data.size() - m_position) {
        return pastEnd(destination.size(), m_position, m_data.size() - m_position);
    }
    std::ranges::copy(m_data.subspan(m_position, destination.size()), destination.begin());
    m_position += destination.size();
    return {};
}

std::expected<void, Error> MemoryStream::seek(std::uint64_t position) {
    if (position > m_data.size()) {
        return seekPastEnd(position, m_data.size());
    }
    m_position = static_cast<std::size_t>(position);
    return {};
}

std::expected<SubStream, Error> SubStream::open(Stream& parent, std::uint64_t length) {
    if (length > parent.remaining()) {
        return pastEnd(length, parent.tell(), parent.remaining());
    }
    return SubStream(parent, parent.tell(), length);
}

std::expected<void, Error> SubStream::read(std::span<std::byte> destination) {
    if (destination.size() > m_length - m_position) {
        return pastEnd(destination.size(), m_position, m_length - m_position);
    }
    // Seek every time: the parent is shared with the code that made the window, which may have moved it.
    if (auto moved = m_parent->seek(m_base + m_position); !moved) {
        return moved;
    }
    if (auto done = m_parent->read(destination); !done) {
        return done;
    }
    m_position += destination.size();
    return {};
}

std::expected<void, Error> SubStream::seek(std::uint64_t position) {
    if (position > m_length) {
        return seekPastEnd(position, m_length);
    }
    m_position = position;
    return {};
}

} // namespace coney::io
