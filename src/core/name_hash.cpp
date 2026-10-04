// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/name_hash.h"

#include <array>

namespace coney {

namespace {

// The 256-entry lookup table of the reflected CRC-32, computed at compile time. The original builds the same table
// at start-up into .bss; a constant table needs no initialisation order.
constexpr std::array<std::uint32_t, 256> makeTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0 ? (value >> 1) ^ 0xEDB88320U : value >> 1;
        }
        table[i] = value;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kTable = makeTable();

// Feeds one byte into a running CRC: the table-driven step both crc32 overloads share.
std::uint32_t update(std::uint32_t crc, std::uint8_t byte) { return kTable[(crc ^ byte) & 0xFFU] ^ (crc >> 8); }

} // namespace

std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte b : bytes) {
        crc = update(crc, static_cast<std::uint8_t>(b));
    }
    return crc ^ 0xFFFFFFFFU;
}

std::uint32_t crc32(std::string_view text) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const char c : text) {
        crc = update(crc, static_cast<std::uint8_t>(c));
    }
    return crc ^ 0xFFFFFFFFU;
}

std::string lowercaseAscii(std::string_view text) {
    std::string lowered(text);
    for (char& c : lowered) {
        // Only A-Z: the names on the disc are ASCII, and folding other bytes by the host's locale would make the hash
        // depend on the machine.
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return lowered;
}

std::uint32_t nameHash(std::string_view path) { return crc32(std::string_view(lowercaseAscii(path))); }

} // namespace coney
