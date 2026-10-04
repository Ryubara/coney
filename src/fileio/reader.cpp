// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/reader.h"

#include <bit>
#include <format>
#include <utility>

#include "core/assert.h"

namespace coney::io {

namespace {

std::unexpected<Error> truncated(std::size_t wanted, std::size_t position, std::size_t remaining) {
    return fail(ErrorCode::Truncated,
                std::format("needed {} bytes at offset {} but only {} remain", wanted, position, remaining));
}

} // namespace

std::uint32_t loadU32Le(std::span<const std::byte> bytes) {
    CONEY_ASSERT(bytes.size() >= 4);
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::expected<std::uint8_t, Error> Reader::readU8() {
    if (remaining() < 1) {
        return truncated(1, m_position, remaining());
    }
    return static_cast<std::uint8_t>(m_data[m_position++]);
}

std::expected<std::uint16_t, Error> Reader::readU16Le() {
    if (remaining() < 2) {
        return truncated(2, m_position, remaining());
    }
    const auto value = static_cast<std::uint16_t>(static_cast<unsigned>(m_data[m_position]) |
                                                  (static_cast<unsigned>(m_data[m_position + 1]) << 8));
    m_position += 2;
    return value;
}

std::expected<std::uint32_t, Error> Reader::readU32Le() {
    if (remaining() < 4) {
        return truncated(4, m_position, remaining());
    }
    const std::uint32_t value = loadU32Le(m_data.subspan(m_position, 4));
    m_position += 4;
    return value;
}

std::expected<float, Error> Reader::readF32Le() {
    auto bits = readU32Le();
    if (!bits) {
        return std::unexpected(std::move(bits.error()));
    }
    return std::bit_cast<float>(*bits);
}

std::expected<std::span<const std::byte>, Error> Reader::readBytes(std::size_t count) {
    if (remaining() < count) {
        return truncated(count, m_position, remaining());
    }
    auto bytes = m_data.subspan(m_position, count);
    m_position += count;
    return bytes;
}

std::expected<void, Error> Reader::seek(std::size_t offset) {
    if (offset > m_data.size()) {
        return fail(ErrorCode::Truncated,
                    std::format("cannot seek to offset {}: the data is {} bytes long", offset, m_data.size()));
    }
    m_position = offset;
    return {};
}

} // namespace coney::io
