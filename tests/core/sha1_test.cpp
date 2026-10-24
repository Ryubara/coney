// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/sha1.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::Sha1;

namespace {

// The digest of `text` as lower-case hex, in one update.
std::string sha1Of(std::string_view text) {
    Sha1 sha;
    sha.update(text);
    return coney::toHex(sha.finish());
}

} // namespace

TEST_CASE("SHA-1 matches the FIPS 180 test vectors", "[sha1]") {
    // FIPS 180-2 appendix A and the empty message every implementation agrees on (hashlib.sha1(b"").hexdigest()).
    CHECK(sha1Of("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(sha1Of("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(sha1Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    // A million 'a's, fed in uneven pieces, so a piece that straddles a 64-byte block is covered.
    Sha1 sha;
    const std::string piece(997, 'a');
    std::size_t left = 1'000'000;
    while (left > 0) {
        const std::size_t take = left < piece.size() ? left : piece.size();
        sha.update(std::string_view(piece).substr(0, take));
        left -= take;
    }
    CHECK(coney::toHex(sha.finish()) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE("SHA-1 pads messages that end near a block boundary", "[sha1]") {
    // 55, 56 and 64 bytes: the lengths where the padding and the length field need one block or two.
    CHECK(sha1Of(std::string(55, 'a')) == "c1c8bbdc22796e28c0e15163d20899b65621d65a");
    CHECK(sha1Of(std::string(56, 'a')) == "c2db330f6083854c99d4b5bfb6e8f29f201be699");
    CHECK(sha1Of(std::string(64, 'a')) == "0098ba824b5c16427bd7a1122a5a442a25ec644d");
}

TEST_CASE("SHA-1 of bytes equals SHA-1 of the same characters", "[sha1]") {
    const std::vector<std::byte> bytes{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    Sha1 sha;
    sha.update(std::span<const std::byte>(bytes));
    CHECK(coney::toHex(sha.finish()) == sha1Of("abc"));
}

TEST_CASE("parseSha1Hex reads 40 hex digits and refuses anything else", "[sha1]") {
    const auto digest = coney::parseSha1Hex("A9993E364706816ABA3E25717850C26C9CD0D89D");
    REQUIRE(digest.has_value());
    // value_or, not *: clang-tidy does not see REQUIRE as a check on the optional.
    CHECK(coney::toHex(digest.value_or(coney::Sha1Digest{})) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK_FALSE(coney::parseSha1Hex("a9993e").has_value());
    CHECK_FALSE(coney::parseSha1Hex("g9993e364706816aba3e25717850c26c9cd0d89d").has_value());
}
