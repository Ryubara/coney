// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace coney {

/// A SHA-256 digest: 32 bytes.
using Sha256Digest = std::array<std::uint8_t, 32>;

/// SHA-256 (FIPS 180-4), fed in pieces. The launcher checks every file it downloads against the SHA-256 its release
/// manifest pins before the file may replace an installed one.
class Sha256 {
  public:
    /// Adds `bytes` to the message.
    void update(std::span<const std::byte> bytes);
    /// Adds the characters of `text`, as bytes, to the message.
    void update(std::string_view text);
    /// Pads the message and returns its digest. The object is spent: make a new one for the next message.
    [[nodiscard]] Sha256Digest finish();

  private:
    // Runs the compression function over one 64-byte block.
    void block(const std::uint8_t* data);

    std::array<std::uint32_t, 8> m_state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                         0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> m_buffer{};
    std::size_t m_buffered = 0;
    std::uint64_t m_length = 0; ///< Message bytes so far.
};

/// The digest as 64 lower-case hex digits, as `sha256sum` prints it.
[[nodiscard]] std::string toHex(const Sha256Digest& digest);

/// Reads 64 hex digits (either case) into a digest; nullopt for any other text.
[[nodiscard]] std::optional<Sha256Digest> parseSha256Hex(std::string_view hex);

} // namespace coney
