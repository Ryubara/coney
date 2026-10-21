// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/sha1.h"

#include <bit>
#include <cstring>

namespace coney {

namespace {

// A big-endian 32-bit word of a block.
std::uint32_t loadU32Be(const std::uint8_t* data) {
    return (std::uint32_t{data[0]} << 24) | (std::uint32_t{data[1]} << 16) | (std::uint32_t{data[2]} << 8) |
           std::uint32_t{data[3]};
}

// The value of one hex digit, or -1.
int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

void Sha1::block(const std::uint8_t* data) {
    // The message schedule: 16 words of the block, then 64 more, each a rotated XOR of four earlier ones.
    std::array<std::uint32_t, 80> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = loadU32Be(data + (4 * i));
    }
    for (std::size_t i = 16; i < 80; ++i) {
        w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    // Eighty rounds in four groups of twenty, each with its own function and constant.
    auto [a, b, c, d, e] = m_state;
    for (std::size_t i = 0; i < 80; ++i) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999U;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        const std::uint32_t next = std::rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = std::rotl(b, 30);
        b = a;
        a = next;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
}

void Sha1::update(std::span<const std::byte> bytes) {
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    std::size_t size = bytes.size();
    m_length += size;
    // Top up a part-filled buffer first, then run whole blocks straight from the input.
    if (m_buffered > 0) {
        const std::size_t take = size < 64 - m_buffered ? size : 64 - m_buffered;
        std::memcpy(m_buffer.data() + m_buffered, data, take);
        m_buffered += take;
        data += take;
        size -= take;
        if (m_buffered < 64) {
            return;
        }
        block(m_buffer.data());
        m_buffered = 0;
    }
    while (size >= 64) {
        block(data);
        data += 64;
        size -= 64;
    }
    if (size > 0) {
        std::memcpy(m_buffer.data(), data, size);
        m_buffered = size;
    }
}

void Sha1::update(std::string_view text) { update(std::as_bytes(std::span(text.data(), text.size()))); }

Sha1Digest Sha1::finish() {
    // Padding: a 1 bit, zeros up to 56 bytes into a block, then the message length in bits, big-endian.
    const std::uint64_t bits = m_length * 8;
    std::array<std::byte, 72> pad{};
    pad[0] = std::byte{0x80};
    const std::size_t zeros = (m_buffered < 56 ? 56 : 120) - m_buffered;
    for (std::size_t i = 0; i < 8; ++i) {
        pad[zeros + i] = static_cast<std::byte>((bits >> (56 - (8 * i))) & 0xFFU);
    }
    update(std::span<const std::byte>(pad.data(), zeros + 8));
    Sha1Digest digest{};
    for (std::size_t i = 0; i < 5; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            digest[(4 * i) + j] = static_cast<std::uint8_t>((m_state[i] >> (24 - (8 * j))) & 0xFFU);
        }
    }
    return digest;
}

std::string toHex(const Sha1Digest& digest) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(40);
    for (const std::uint8_t byte : digest) {
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

std::optional<Sha1Digest> parseSha1Hex(std::string_view hex) {
    if (hex.size() != 40) {
        return std::nullopt;
    }
    Sha1Digest digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const int high = hexValue(hex[2 * i]);
        const int low = hexValue(hex[(2 * i) + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        digest[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return digest;
}

} // namespace coney
