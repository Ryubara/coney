// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_index.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/sha256.h"
#include "fileio/stream.h"
#include "support/cpak_fixtures.h"

using coney::ErrorCode;
using coney::cpak::Compression;
using coney::cpak::Index;
using coney::test::cpak::buildArchive;
using coney::test::cpak::bytesOf;
using coney::test::cpak::failure;
using coney::test::cpak::RawEntry;
using coney::test::cpak::rawEntry;
using coney::test::cpak::RawHeader;
using coney::test::cpak::sha256Of;

namespace {

// Two well-formed entries at 16-byte alignment: the index ends at 64 + 2 * 64 + 10 = 202, so the first entry is at 208.
std::vector<RawEntry> twoEntries() { return {rawEntry("a/one", 208, "first"), rawEntry("b/two", 224, "second!")}; }

// Index::parse of the whole of `bytes`.
std::expected<Index, coney::Error> parseAll(const std::vector<std::byte>& bytes) {
    return Index::parse(bytes, bytes.size());
}

} // namespace

TEST_CASE("the index of a hand-built archive is read as the format page lays it out", "[cpak]") {
    // Expected: ../guides/cpak-format.md#header, Coney's own format (also #records and #names)
    const auto bytes = buildArchive(twoEntries());
    const auto index = parseAll(bytes);
    REQUIRE(index.has_value());
    CHECK(index->alignment() == 16);
    CHECK(index->archiveSize() == bytes.size());
    CHECK(index->frontSize() == 202);
    REQUIRE(index->entries().size() == 2);
    const auto& second = index->entries()[1];
    CHECK(second.name == "b/two");
    CHECK(second.offset == 224);
    CHECK(second.storedSize == 7);
    CHECK(second.size == 7);
    CHECK(second.compression == Compression::None);
    CHECK(second.sha256 == sha256Of(bytesOf("second!")));
    REQUIRE(index->find("a/one") != nullptr);
    CHECK(index->find("a/one")->offset == 208);
    CHECK(index->find("A/ONE") == nullptr);
    CHECK(index->find("a/on") == nullptr);
}

TEST_CASE("an empty archive is only its header", "[cpak]") {
    // Expected: ../guides/cpak-format.md#header, Coney's own format
    const auto bytes = buildArchive({});
    CHECK(bytes.size() == coney::cpak::kHeaderSize);
    const auto index = parseAll(bytes);
    REQUIRE(index.has_value());
    CHECK(index->entries().empty());
}

TEST_CASE("a damaged index is refused by its hash", "[cpak]") {
    // Expected: ../guides/cpak-format.md#hashes, Coney's own format (the index hash covers the header's first 32
    // bytes and the index)
    const auto good = buildArchive(twoEntries());
    // The archive undamaged reads, so each refusal below is the damage's.
    REQUIRE(parseAll(good).has_value());
    // One byte flipped in a name, in a record's size, in the alignment and in the hash itself.
    for (const std::size_t at : {std::size_t{200}, std::size_t{64 + 0x08}, std::size_t{0x08}, std::size_t{0x30}}) {
        INFO("byte " << at);
        auto bytes = good;
        bytes[at] ^= std::byte{0x01};
        const auto index = parseAll(bytes);
        REQUIRE_FALSE(index.has_value());
        CHECK(failure(index) == ErrorCode::Invalid);
    }
}

TEST_CASE("a header that is not a Coney Pack of this version is refused", "[cpak]") {
    // Expected: ../guides/cpak-format.md#header, Coney's own format
    auto wrongMagic = buildArchive(twoEntries());
    wrongMagic[0] = std::byte{'X'};
    CHECK(failure(parseAll(wrongMagic)) == ErrorCode::Invalid);
    CHECK(failure(parseAll(buildArchive(twoEntries(), RawHeader{.version = 2}))) == ErrorCode::Invalid);
    CHECK(failure(parseAll(buildArchive(twoEntries(), RawHeader{.flags = 1}))) == ErrorCode::Invalid);
    CHECK(failure(parseAll(buildArchive(twoEntries(), RawHeader{.alignment = 24}))) == ErrorCode::Invalid);

    // Cut short: inside the header, inside the index, and an archive smaller than its header's size.
    const auto bytes = buildArchive(twoEntries());
    CHECK(failure(Index::parse(std::span(bytes).first(40), bytes.size())) == ErrorCode::Truncated);
    CHECK(failure(Index::parse(std::span(bytes).first(150), bytes.size())) == ErrorCode::Truncated);
    CHECK(failure(Index::parse(bytes, bytes.size() - 1)) == ErrorCode::Truncated);
    CHECK(failure(Index::parse(bytes, bytes.size() + 1)) == ErrorCode::Invalid);
}

TEST_CASE("records that break the rules are refused even when the index hash matches", "[cpak]") {
    // Expected: ../guides/cpak-format.md#reading, Coney's own format
    const auto refused = [](const std::vector<RawEntry>& entries) {
        const auto index = parseAll(buildArchive(entries));
        REQUIRE_FALSE(index.has_value());
        CHECK(failure(index) == ErrorCode::Invalid);
    };
    // The same shapes made well read, so each refusal below is the broken rule's.
    REQUIRE(parseAll(buildArchive(twoEntries())).has_value());
    SECTION("names out of order") { refused({rawEntry("b/two", 208, "first"), rawEntry("a/one", 224, "second!")}); }
    SECTION("a name twice") { refused({rawEntry("a/one", 208, "first"), rawEntry("a/one", 224, "second!")}); }
    SECTION("an ill-formed name") { refused({rawEntry("a/../b", 208, "first")}); }
    SECTION("an offset off the alignment") { refused({rawEntry("a/one", 209, "first")}); }
    SECTION("an entry inside the index") {
        // Its bytes are left out, so the records it would overlap stay as built.
        auto entry = rawEntry("a/one", 64, "first");
        entry.stored.clear();
        refused({entry});
    }
    SECTION("two entries that overlap") {
        refused({rawEntry("a/one", 208, "0123456789abcdefXY"), rawEntry("b/two", 224, "second!")});
    }
    SECTION("an entry past the end of the archive") {
        auto entry = rawEntry("a/one", 208, "first");
        entry.storedSize = 6;
        refused({entry});
    }
    SECTION("an uncompressed entry whose sizes differ") {
        auto entry = rawEntry("a/one", 208, "first");
        entry.size = 4;
        refused({entry});
    }
    SECTION("an unknown compression") {
        auto entry = rawEntry("a/one", 208, "first");
        entry.compression = 3;
        refused({entry});
    }
}

TEST_CASE("checkName accepts relative paths and refuses what could not be one", "[cpak]") {
    // Expected: ../guides/cpak-format.md#names, Coney's own format
    for (const char* good : {"a", "textures/hud/hud_main", "worlds/level99s_sec.wld", "a.b/c-d_e", "0x1a2b3c4d"}) {
        INFO(good);
        CHECK(coney::cpak::checkName(good).has_value());
    }
    for (const char* bad : {"", "/", "/a", "a/", "a//b", ".", "..", "./a", "a/.", "a\\b", "c:/a", "a\nb"}) {
        INFO(bad);
        const auto checked = coney::cpak::checkName(bad);
        REQUIRE_FALSE(checked.has_value());
        CHECK(failure(checked) == ErrorCode::Invalid);
    }
    CHECK_FALSE(coney::cpak::checkName(std::string("a\0b", 3)).has_value());
}

TEST_CASE("an entry whose bytes do not match its hash is refused", "[cpak]") {
    // Expected: ../guides/cpak-format.md#hashes, Coney's own format
    const auto bytes = buildArchive(twoEntries());
    const auto index = parseAll(bytes);
    REQUIRE(index.has_value());
    const auto& entry = index->entries()[0];
    CHECK(coney::cpak::verifyEntry(entry, bytesOf("first")).has_value());
    CHECK(failure(coney::cpak::verifyEntry(entry, bytesOf("First"))) == ErrorCode::Invalid);
    CHECK(failure(coney::cpak::verifyEntry(entry, bytesOf("firs"))) == ErrorCode::Invalid);
}

TEST_CASE("an index and its entries are read from a stream, and a damaged entry is refused", "[cpak]") {
    // Expected: ../guides/cpak-format.md#reading, Coney's own format
    auto bytes = buildArchive(twoEntries());
    coney::io::MemoryStream stream(bytes);
    const auto index = Index::read(stream);
    REQUIRE(index.has_value());
    const auto second = coney::cpak::readEntry(stream, index->entries()[1]);
    REQUIRE(second.has_value());
    CHECK(*second == bytesOf("second!"));

    // The entry's bytes damaged after the index was read: the index still reads, the entry does not.
    bytes[224] = std::byte{'S'};
    coney::io::MemoryStream damaged(bytes);
    const auto again = Index::read(damaged);
    REQUIRE(again.has_value());
    CHECK(failure(coney::cpak::readEntry(damaged, again->entries()[1])) == ErrorCode::Invalid);
    CHECK(coney::cpak::readEntry(damaged, again->entries()[0]).has_value());
}

TEST_CASE("a compressed entry is listed but not read, as this reader decodes none", "[cpak]") {
    // Expected: ../guides/cpak-format.md#compression, Coney's own format (installs write none)
    auto entry = rawEntry("a/one", 208, "first");
    entry.compression = static_cast<std::uint8_t>(Compression::Zstd);
    entry.size = 100; // A decompressed size may differ from the stored one.
    const auto bytes = buildArchive({entry});
    const auto index = parseAll(bytes);
    REQUIRE(index.has_value());
    CHECK(index->entries()[0].compression == Compression::Zstd);
    CHECK(coney::cpak::compressionName(Compression::Zstd) == "zstd");
    const auto verified = coney::cpak::verifyEntry(index->entries()[0], bytesOf("first"));
    REQUIRE_FALSE(verified.has_value());
    CHECK(failure(verified) == ErrorCode::Invalid);
    CHECK(verified.error().message.find("zstd") != std::string::npos);
}

TEST_CASE("checkName refuses a name that is not well-formed UTF-8", "[cpak]") {
    // Expected: ../guides/cpak-format.md#names, Coney's own format (strict UTF-8: no overlong forms, no surrogates,
    // nothing past U+10FFFF), the rule the Python reader applies too
    for (const char* good : {"caf\xc3\xa9", "\xe6\x97\xa5/\xe6\x9c\xac", "a/\xf0\x9f\x98\x80", "\xf4\x8f\xbf\xbf"}) {
        INFO(good);
        CHECK(coney::cpak::checkName(good).has_value());
    }
    for (const char* bad : {"\xff", "a\x80", "\xc3", "a/\xe6\x97", "\xc0\xaf", "\xe0\x80\xaf", "\xed\xa0\x80",
                            "\xf4\x90\x80\x80", "\xf8\x88\x80\x80\x80"}) {
        INFO(bad);
        CHECK(failure(coney::cpak::checkName(bad)) == ErrorCode::Invalid);
    }
}

TEST_CASE("an entry count larger than its index holds is refused, and so is no input at all", "[cpak]") {
    // Expected: ../guides/cpak-format.md#reading, Coney's own format (the count is checked against the index before
    // records are read or reserved)
    const auto good = buildArchive(twoEntries());
    // The archive as built reads, so each refusal below is the count's.
    REQUIRE(parseAll(good).has_value());
    for (const std::uint32_t count : {std::uint32_t{3}, std::uint32_t{0x0400'0000}, std::uint32_t{0xffff'ffff}}) {
        INFO("count " << count);
        auto bytes = good;
        coney::test::cpak::put(bytes, 0x0c, count, 4);
        // Hashed again as the page says, so the index hash matches and only the count is wrong.
        coney::Sha256 sha;
        sha.update(std::span<const std::byte>(bytes).first(32));
        sha.update(std::span<const std::byte>(bytes).subspan(coney::cpak::kHeaderSize, 202 - coney::cpak::kHeaderSize));
        const auto digest = sha.finish();
        for (std::size_t i = 0; i < digest.size(); ++i) {
            bytes[0x20 + i] = static_cast<std::byte>(digest[i]);
        }
        CHECK(failure(parseAll(bytes)) == ErrorCode::Invalid);
        coney::io::MemoryStream stream(bytes);
        CHECK(failure(Index::read(stream)) == ErrorCode::Invalid);
    }
    const std::vector<std::byte> none;
    CHECK(failure(Index::parse(none, 0)) == ErrorCode::Truncated);
    coney::io::MemoryStream empty(none);
    CHECK(failure(Index::read(empty)) == ErrorCode::Truncated);
}
