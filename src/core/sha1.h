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

/// A SHA-1 digest: 20 bytes.
using Sha1Digest = std::array<std::uint8_t, 20>;

/// SHA-1 (FIPS 180-4), fed in pieces. The installer checks each file it copies from the player's disc against the
/// known SHA-1s (docs/research/discs.md), the same digests `coney-tools` prints, so the two can be compared.
///
/// SHA-1 is used to identify files, not for security: the release downloads are checked with SHA-256.
class Sha1 {
  public:
    /// Adds `bytes` to the message.
    void update(std::span<const std::byte> bytes);
    /// Adds the characters of `text`, as bytes, to the message.
    void update(std::string_view text);
    /// Pads the message and returns its digest. The object is spent: make a new one for the next message.
    [[nodiscard]] Sha1Digest finish();

  private:
    // Runs the compression function over one 64-byte block.
    void block(const std::uint8_t* data);

    std::array<std::uint32_t, 5> m_state{0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
    std::array<std::uint8_t, 64> m_buffer{};
    std::size_t m_buffered = 0;
    std::uint64_t m_length = 0; ///< Message bytes so far.
};

/// The digest as 40 lower-case hex digits, as `sha1sum` and Python's hashlib print it.
[[nodiscard]] std::string toHex(const Sha1Digest& digest);

/// Reads 40 hex digits (either case) into a digest; nullopt for any other text.
[[nodiscard]] std::optional<Sha1Digest> parseSha1Hex(std::string_view hex);

} // namespace coney
