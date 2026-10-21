// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/sha256.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::Sha256;

namespace {

// The digest of `text` as lower-case hex, in one update.
std::string sha256Of(std::string_view text) {
    Sha256 sha;
    sha.update(text);
    return coney::toHex(sha.finish());
}

} // namespace

TEST_CASE("SHA-256 matches the FIPS 180 test vectors", "[sha256]") {
    CHECK(sha256Of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // A million 'a's, fed in uneven pieces, so a piece that straddles a 64-byte block is covered.
    Sha256 sha;
    const std::string piece(997, 'a');
    std::size_t left = 1'000'000;
    while (left > 0) {
        const std::size_t take = left < piece.size() ? left : piece.size();
        sha.update(std::string_view(piece).substr(0, take));
        left -= take;
    }
    CHECK(coney::toHex(sha.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 pads messages that end near a block boundary", "[sha256]") {
    CHECK(sha256Of(std::string(55, 'a')) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(sha256Of(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(sha256Of(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST_CASE("parseSha256Hex reads 64 hex digits and refuses anything else", "[sha256]") {
    const auto digest = coney::parseSha256Hex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
    REQUIRE(digest.has_value());
    CHECK(coney::toHex(*digest) == sha256Of("abc"));
    CHECK_FALSE(coney::parseSha256Hex("ba7816bf").has_value());
    CHECK_FALSE(coney::parseSha256Hex(std::string(64, 'z')).has_value());
}
