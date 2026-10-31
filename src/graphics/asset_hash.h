// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// @coney-only renderer data: the hash the mesh and texture formats print in place of their data
// (docs/engine/asset-formats.md)

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace coney::graphics {

/// A 64-bit FNV-1a hash fed value by value. Each value goes in as its little-endian bytes (a float as its bits, a bool
/// as one byte, a size as 64 bits), so the hash is the same on every platform and compiler, whatever their padding.
class AssetHash {
  public:
    /// Adds one integer, enum-free bool or float.
    template <typename T>
        requires std::is_arithmetic_v<T>
    AssetHash& add(T value) {
        if constexpr (std::is_same_v<T, bool>) {
            return addBytes(value ? 1U : 0U, 1);
        } else if constexpr (std::is_same_v<T, float>) {
            return addBytes(std::bit_cast<std::uint32_t>(value), 4);
        } else if constexpr (std::is_same_v<T, std::size_t> && sizeof(std::size_t) != 8) {
            return addBytes(static_cast<std::uint64_t>(value), 8);
        } else {
            static_assert(std::is_integral_v<T>, "a double has no place in the formats");
            return addBytes(static_cast<std::uint64_t>(value), sizeof(T));
        }
    }

    /// Adds every element of `values`, in order.
    template <typename T, std::size_t N> AssetHash& add(const std::array<T, N>& values) {
        for (const T& value : values) {
            add(value);
        }
        return *this;
    }

    /// Adds a string's length, then its bytes, so "ab" + "c" and "a" + "bc" differ.
    AssetHash& add(std::string_view text) {
        add(static_cast<std::uint64_t>(text.size()));
        for (const char c : text) {
            addBytes(static_cast<std::uint8_t>(c), 1);
        }
        return *this;
    }

    /// The hash of everything added so far.
    [[nodiscard]] std::uint64_t value() const { return m_hash; }

  private:
    // Feeds the low `count` bytes of `bits`, lowest first.
    AssetHash& addBytes(std::uint64_t bits, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            m_hash = (m_hash ^ ((bits >> (8 * i)) & 0xFFU)) * 0x100000001b3ULL;
        }
        return *this;
    }

    std::uint64_t m_hash = 0xcbf29ce484222325ULL;
};

} // namespace coney::graphics
