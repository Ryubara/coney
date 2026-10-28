// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_writer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/cpak_archive.h"
#include "core/cpak_index.h"
#include "core/sha256.h"
#include "support/cpak_fixtures.h"

using coney::ErrorCode;
using coney::cpak::Archive;
using coney::cpak::Writer;
using coney::test::cpak::bytesOf;
using coney::test::cpak::sha256Of;

namespace {

// The format page's sample archive: three synthetic entries, one empty, added out of name order, 16-byte alignment.
Writer sampleWriter(const std::vector<std::byte>& readme, const std::vector<std::byte>& logo,
                    const std::vector<std::byte>& empty) {
    Writer writer(16);
    REQUIRE(writer.add("readme", readme).has_value());
    REQUIRE(writer.add("art/logo", logo).has_value());
    REQUIRE(writer.add("empty", empty).has_value());
    return writer;
}

} // namespace

TEST_CASE("a Coney Pack written and read back holds every entry, sorted by name", "[cpak]") {
    // Expected: ../guides/cpak-format.md#writing, Coney's own format (entries sorted by name, byte order)
    const auto readme = bytesOf("Coney");
    const std::vector<std::byte> logo{std::byte{0}, std::byte{1}, std::byte{2}};
    const std::vector<std::byte> empty;
    const auto bytes = sampleWriter(readme, logo, empty).toBytes();
    REQUIRE(bytes.has_value());
    const auto archive = Archive::open(*bytes);
    REQUIRE(archive.has_value());
    const auto& entries = archive->index().entries();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].name == "art/logo");
    CHECK(entries[1].name == "empty");
    CHECK(entries[2].name == "readme");
    for (const auto& [name, data] :
         {std::pair{"art/logo", logo}, std::pair{"empty", empty}, std::pair{"readme", readme}}) {
        INFO(name);
        const auto read = archive->read(name);
        REQUIRE(read.has_value());
        CHECK(std::vector<std::byte>(read->begin(), read->end()) == data);
        const auto* entry = archive->index().find(name);
        REQUIRE(entry != nullptr);
        CHECK(entry->size == data.size());
        CHECK(entry->storedSize == data.size());
        CHECK(entry->compression == coney::cpak::Compression::None);
        CHECK(entry->sha256 == sha256Of(data));
    }
}

TEST_CASE("the writer's sample archive is the format page's byte for byte", "[cpak]") {
    // Expected: ../guides/cpak-format.md#example, Coney's own format (309 bytes; the SHA-256 the page and
    // python/tests/test_cpak.py give for the same three entries)
    const auto readme = bytesOf("Coney");
    const std::vector<std::byte> logo{std::byte{0}, std::byte{1}, std::byte{2}};
    const std::vector<std::byte> empty;
    const auto bytes = sampleWriter(readme, logo, empty).toBytes();
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() == 309);
    CHECK(coney::toHex(sha256Of(*bytes)) == "188b0b7a40e4125e66ddf7ae6b81c61436070adc0e2406d5947b85441df6c486");
}

TEST_CASE("every entry starts at a multiple of the alignment, with zero bytes before it", "[cpak]") {
    // Expected: ../guides/cpak-format.md#alignment, Coney's own format
    for (const std::uint32_t alignment : {16U, 2048U, 4096U}) {
        INFO("alignment " << alignment);
        Writer writer(alignment);
        std::vector<std::vector<std::byte>> payloads;
        payloads.reserve(5);
        for (std::size_t i = 0; i < 5; ++i) {
            // Odd sizes, so every entry but the first ends off the alignment.
            payloads.emplace_back(i * 37 + 1, static_cast<std::byte>(0xa0 + i));
        }
        for (std::size_t i = 0; i < payloads.size(); ++i) {
            REQUIRE(writer.add("e" + std::to_string(i), payloads[i]).has_value());
        }
        const auto bytes = writer.toBytes();
        REQUIRE(bytes.has_value());
        const auto archive = Archive::open(*bytes);
        REQUIRE(archive.has_value());
        CHECK(archive->index().alignment() == alignment);
        std::uint64_t previousEnd = archive->index().frontSize();
        for (const auto& entry : archive->index().entries()) {
            CHECK(entry.offset % alignment == 0);
            CHECK(entry.offset >= previousEnd);
            for (std::uint64_t at = previousEnd; at < entry.offset; ++at) {
                CHECK((*bytes)[at] == std::byte{0});
            }
            previousEnd = entry.offset + entry.storedSize;
        }
        CHECK(previousEnd == bytes->size());
    }
}

TEST_CASE("the writer refuses a bad name, a name used twice in any case, and a bad alignment", "[cpak]") {
    // Expected: ../guides/cpak-format.md#names, Coney's own format
    const auto data = bytesOf("x");
    Writer writer(16);
    REQUIRE(writer.add("Textures/HUD", data).has_value());
    for (const char* bad : {"", "/a", "a/", "a//b", "a/./b", "a/../b", "..", "a\\b", "c:x", "a\tb"}) {
        INFO(bad);
        const auto added = writer.add(bad, data);
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().code == ErrorCode::Invalid);
    }
    CHECK_FALSE(writer.add(std::string(coney::cpak::kMaxNameBytes + 1, 'a'), data).has_value());
    CHECK(writer.add(std::string(coney::cpak::kMaxNameBytes, 'a'), data).has_value());
    const auto twice = writer.add("textures/hud", data);
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().code == ErrorCode::Invalid);

    for (const std::uint32_t alignment : {0U, 8U, 24U, coney::cpak::kMaxAlignment * 2}) {
        INFO("alignment " << alignment);
        Writer bad(alignment);
        const auto written = bad.toBytes();
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("a fetched entry is written in pieces and refused when its bytes are not the ones added", "[cpak]") {
    // Expected: ../guides/cpak-format.md#writing, Coney's own format
    const auto data = bytesOf("abcdefghij");
    const auto digest = sha256Of(data);
    // Hands the bytes over three at a time, as a reader of a large file would.
    const auto inPieces = [&data](const coney::cpak::Sink& sink) -> std::expected<void, coney::Error> {
        for (std::size_t at = 0; at < data.size(); at += 3) {
            const std::size_t take = std::min<std::size_t>(3, data.size() - at);
            if (auto put = sink(std::span<const std::byte>(data).subspan(at, take)); !put) {
                return put;
            }
        }
        return {};
    };
    Writer writer(16);
    REQUIRE(writer.add("big", data.size(), digest, inPieces).has_value());
    const auto bytes = writer.toBytes();
    REQUIRE(bytes.has_value());
    const auto archive = Archive::open(*bytes);
    REQUIRE(archive.has_value());
    const auto read = archive->read("big");
    REQUIRE(read.has_value());
    CHECK(std::vector<std::byte>(read->begin(), read->end()) == data);

    // A source whose bytes changed since it was added: the same size, another hash.
    Writer changed(16);
    auto other = sha256Of(bytesOf("abcdefghiJ"));
    REQUIRE(changed.add("big", data.size(), other, inPieces).has_value());
    const auto refused = changed.toBytes();
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::Invalid);

    // A source shorter than it said.
    Writer shorter(16);
    REQUIRE(shorter.add("big", data.size() + 1, digest, inPieces).has_value());
    CHECK_FALSE(shorter.toBytes().has_value());
}
