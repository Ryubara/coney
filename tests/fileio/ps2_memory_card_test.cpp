// SPDX-License-Identifier: GPL-3.0-or-later
// The PS2 memory card reader and the PSU export reader, on synthetic cards (docs/research/save.md#import-containers).
#include "fileio/ps2_memory_card.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"
#include "support/ps2_card_fixtures.h"

using coney::ErrorCode;
using coney::io::looksLikePsu;
using coney::io::Ps2MemoryCard;
using coney::io::readPsuExport;
using coney::test::CardFolder;
using coney::test::CardLayout;

namespace {

// `count` bytes counting up from `seed`: content whose every byte shows where it came from.
std::vector<std::byte> counting(std::size_t count, std::uint8_t seed) {
    std::vector<std::byte> bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::byte>((seed + i) & 0xffU);
    }
    return bytes;
}

// Two saves: a game's folder with a file over three clusters and a small one, and another game's.
std::vector<CardFolder> twoSaves() {
    return {
        {"BASLUS-21215", {{"BASLUS-21215", counting(2500, 1)}, {"icon.sys", counting(64, 9)}}},
        {"BESLES-00000", {{"DATA", counting(10, 3)}}},
    };
}

} // namespace

TEST_CASE("a PCSX2 card image with spares lists its folders and reads a file across clusters", "[ps2_memory_card]") {
    // Expected: save.md#import-containers, inferred (Sony's card file system as card tools and PCSX2 read it)
    const auto image = coney::test::buildPs2Card(twoSaves());
    REQUIRE(Ps2MemoryCard::looksLikeCard(image));
    const auto card = Ps2MemoryCard::parse(image);
    REQUIRE(card.has_value());
    CHECK(card->hasSpares());
    CHECK(card->clusterCount() == 256);
    const auto folders = card->folders();
    REQUIRE(folders.has_value());
    CHECK(*folders == std::vector<std::string>{"BASLUS-21215", "BESLES-00000"});

    const auto save = card->folder("BASLUS-21215");
    REQUIRE(save.has_value());
    REQUIRE(save->files.size() == 2);
    REQUIRE(save->file("BASLUS-21215") != nullptr);
    CHECK(save->file("BASLUS-21215")->data == counting(2500, 1));
    CHECK(save->file("icon.sys")->data == counting(64, 9));
    CHECK(save->file("WARR.ICO") == nullptr);
}

TEST_CASE("a card dump without spares and with scattered clusters reads the same", "[ps2_memory_card]") {
    // Expected: save.md#import-containers, inferred (a chain is followed through the FAT, not by position)
    CardLayout layout;
    layout.spares = false;
    layout.fragmented = true;
    const auto image = coney::test::buildPs2Card(twoSaves(), layout);
    const auto card = Ps2MemoryCard::parse(image);
    REQUIRE(card.has_value());
    CHECK_FALSE(card->hasSpares());
    const auto save = card->folder("BASLUS-21215");
    REQUIRE(save.has_value());
    REQUIRE(save->file("BASLUS-21215") != nullptr);
    CHECK(save->file("BASLUS-21215")->data == counting(2500, 1));
}

TEST_CASE("a card without the folder asked for says so", "[ps2_memory_card]") {
    // Expected: save.md#import-containers, inferred
    const auto image = coney::test::buildPs2Card({{"BESLES-00000", {{"DATA", counting(10, 3)}}}});
    const auto card = Ps2MemoryCard::parse(image);
    REQUIRE(card.has_value());
    const auto save = card->folder("BASLUS-21215");
    REQUIRE_FALSE(save.has_value());
    CHECK(save.error().code == ErrorCode::NotFound);
}

TEST_CASE("an image that is not a card, or of the wrong size, is refused", "[ps2_memory_card]") {
    // Expected: save.md#import-containers, inferred
    auto image = coney::test::buildPs2Card(twoSaves());
    SECTION("no magic") {
        image[0] = std::byte{'X'};
        CHECK_FALSE(Ps2MemoryCard::looksLikeCard(image));
        const auto card = Ps2MemoryCard::parse(image);
        REQUIRE_FALSE(card.has_value());
        CHECK(card.error().code == ErrorCode::Invalid);
    }
    SECTION("cut short") {
        image.resize(image.size() - 528);
        const auto card = Ps2MemoryCard::parse(image);
        REQUIRE_FALSE(card.has_value());
        CHECK(card.error().code == ErrorCode::Invalid);
        CHECK(card.error().message.find("size") != std::string::npos);
    }
    SECTION("too short for a superblock") {
        image.resize(100);
        const auto card = Ps2MemoryCard::parse(image);
        REQUIRE_FALSE(card.has_value());
        CHECK(card.error().code == ErrorCode::Truncated);
    }
}

namespace {

// Writes `value` little-endian at `offset` of `image`.
void poke(std::vector<std::byte>& image, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        image.at(offset + i) = static_cast<std::byte>((value >> (8 * i)) & 0xffU);
    }
}

} // namespace

TEST_CASE("a cluster chain that loops or leaves the card is refused, not followed for ever", "[ps2_memory_card]") {
    // Expected: save.md#import-containers, inferred
    // Without spares and unfragmented, the fixture lays out: the FAT in cluster 2 (bytes 2048-3071), allocatable
    // clusters from 3; the root in allocatable 0-1 (three entries), the file in 2-4, its folder's directory in 5-6, so
    // the file's entry is the first of absolute cluster 9.
    CardLayout layout;
    layout.spares = false;
    auto image = coney::test::buildPs2Card({{"BASLUS-21215", {{"BASLUS-21215", counting(2500, 1)}}}}, layout);
    constexpr std::size_t kFat = 2048;
    // The byte offset of the FAT's entry for a cluster (four bytes each).
    constexpr auto fatEntryAt = [](std::size_t cluster) { return kFat + cluster * 4; };
    constexpr std::size_t kFileLength = 9 * 1024 + 4;
    const auto untouched = Ps2MemoryCard::parse(image);
    REQUIRE(untouched.has_value());
    REQUIRE(untouched->folder("BASLUS-21215").has_value());

    SECTION("a loop") {
        poke(image, fatEntryAt(4), 0x80000002U); // the file's last cluster points back at its first
        poke(image, kFileLength, 100 * 1024);    // and its entry claims more than three clusters
    }
    SECTION("off the card") {
        poke(image, fatEntryAt(4), 0x80000000U | 5000U);
        poke(image, kFileLength, 4000);
    }
    SECTION("a free cluster in the chain") { poke(image, fatEntryAt(3), 0x7fffffffU); }
    const auto card = Ps2MemoryCard::parse(image);
    REQUIRE(card.has_value());
    const auto save = card->folder("BASLUS-21215");
    REQUIRE_FALSE(save.has_value());
    CHECK(save.error().code == ErrorCode::Invalid);
}

TEST_CASE("a PSU export reads its folder and files", "[ps2_memory_card]") {
    // Expected: save.md#import-containers, inferred (the export format as mymc and uLaunchELF write it)
    const CardFolder folder{"BASLUS-21215", {{"BASLUS-21215", counting(2500, 1)}, {"icon.sys", counting(64, 9)}}};
    const auto psu = coney::test::buildPsu(folder);
    CHECK(looksLikePsu(psu));
    CHECK_FALSE(Ps2MemoryCard::looksLikeCard(psu));
    const auto save = readPsuExport(psu);
    REQUIRE(save.has_value());
    CHECK(save->name == "BASLUS-21215");
    REQUIRE(save->files.size() == 2);
    CHECK(save->file("BASLUS-21215")->data == counting(2500, 1));
    CHECK(save->file("icon.sys")->data == counting(64, 9));
}

TEST_CASE("a PSU export cut short or not starting with a folder is refused", "[ps2_memory_card]") {
    // Expected: save.md#import-containers, inferred
    const CardFolder folder{"BASLUS-21215", {{"BASLUS-21215", counting(2500, 1)}}};
    auto psu = coney::test::buildPsu(folder);
    SECTION("cut short") {
        psu.resize(psu.size() - 1024);
        const auto save = readPsuExport(psu);
        REQUIRE_FALSE(save.has_value());
        CHECK(save.error().code == ErrorCode::Truncated);
    }
    SECTION("a file first") {
        psu[0x20 - 0x20] = std::byte{0x97}; // mode 0x8497: a file's
        psu[1] = std::byte{0x84};
        CHECK_FALSE(looksLikePsu(psu));
        const auto save = readPsuExport(psu);
        REQUIRE_FALSE(save.has_value());
        CHECK(save.error().code == ErrorCode::Invalid);
    }
}
