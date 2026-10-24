// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/sha256.h"

#include <bit>
#include <cstring>

namespace coney {

namespace {

// The round constants: the first 32 bits of the fractional parts of the cube roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> kRound{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

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

void Sha256::block(const std::uint8_t* data) {
    // The message schedule: 16 words of the block, then 48 mixed from earlier ones.
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = loadU32Be(data + (4 * i));
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    // Sixty-four rounds over the eight working words.
    auto [a, b, c, d, e, f, g, h] = m_state;
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + choose + kRound[i] + w[i];
        const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    const std::array<std::uint32_t, 8> add{a, b, c, d, e, f, g, h};
    for (std::size_t i = 0; i < 8; ++i) {
        m_state[i] += add[i];
    }
}

void Sha256::update(std::span<const std::byte> bytes) {
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

void Sha256::update(std::string_view text) { update(std::as_bytes(std::span(text.data(), text.size()))); }

Sha256Digest Sha256::finish() {
    // Padding: a 1 bit, zeros up to 56 bytes into a block, then the message length in bits, big-endian.
    const std::uint64_t bits = m_length * 8;
    std::array<std::byte, 72> pad{};
    pad[0] = std::byte{0x80};
    const std::size_t zeros = (m_buffered < 56 ? 56 : 120) - m_buffered;
    for (std::size_t i = 0; i < 8; ++i) {
        pad[zeros + i] = static_cast<std::byte>((bits >> (56 - (8 * i))) & 0xFFU);
    }
    update(std::span<const std::byte>(pad.data(), zeros + 8));
    Sha256Digest digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            digest[(4 * i) + j] = static_cast<std::uint8_t>((m_state[i] >> (24 - (8 * j))) & 0xFFU);
        }
    }
    return digest;
}

std::string toHex(const Sha256Digest& digest) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(64);
    for (const std::uint8_t byte : digest) {
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

std::optional<Sha256Digest> parseSha256Hex(std::string_view hex) {
    if (hex.size() != 64) {
        return std::nullopt;
    }
    Sha256Digest digest{};
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
