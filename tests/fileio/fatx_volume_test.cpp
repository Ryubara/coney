// SPDX-License-Identifier: GPL-3.0-or-later
// The FATX reader, on synthetic volumes built by the tests (docs/research/save.md#xbox-containers).
#include "fileio/fatx_volume.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fileio/stream.h"
#include "support/fixtures.h"
#include "support/xbox_save_fixtures.h"

using coney::ErrorCode;
using coney::io::FatxEntry;
using coney::io::FatxVolume;
using coney::io::MemoryStream;
using coney::test::buildFatx;
using coney::test::flatten;

namespace {

// `count` bytes counting up from `start`, so a byte read from the wrong place shows.
std::vector<std::byte> pattern(std::size_t count, std::uint8_t start) {
    std::vector<std::byte> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<std::byte>(static_cast<std::uint8_t>(start + i));
    }
    return out;
}

// The entry called `name` in `entries`, or an empty one.
FatxEntry named(const std::vector<FatxEntry>& entries, const std::string& name) {
    for (const FatxEntry& entry : entries) {
        if (entry.name == name) {
            return entry;
        }
    }
    FAIL_CHECK("no entry " << name); // FAIL_CHECK goes on, so the return below is reachable (C4702 under /WX)
    return {};
}

} // namespace

// NOLINTBEGIN(bugprone-implicit-widening-of-multiplication-result): the volumes are 1 MB, written 1024 * 1024 in the
// tests' own words; the product is a constant far below int's range, so nothing widens wrongly.
TEST_CASE("a FATX volume lists its folders and reads a file across clusters", "[fatx]") {
    // Expected: save.md#xbox-containers, confirmed (runtime), run r-20261119-xbox-hdd-fatx-layout (the superblock, the
    // FAT after it rounded to 4 KB, clusters from 1, 64-byte entries)
    const auto big = pattern(40000, 7); // three 16 KB clusters
    const auto image = flatten(buildFatx(1024 * 1024, {{"UDATA/5454009b/AB/profile.dat", big},
                                                       {"UDATA/5454009b/TitleMeta.xbx", pattern(50, 1)},
                                                       {"TDATA/x", pattern(3, 9)}}),
                               1024 * 1024);
    MemoryStream stream(image);
    CHECK(FatxVolume::looksLikeFatx(stream, 0));
    auto volume = FatxVolume::open(stream, 0, image.size());
    REQUIRE(volume.has_value());
    CHECK(volume->clusterBytes() == 16384);
    CHECK(volume->clusterCount() == 64);
    CHECK(volume->fatEntryBytes() == 2);
    const auto root = volume->root();
    REQUIRE(root.has_value());
    REQUIRE(root->size() == 2);
    CHECK((*root)[0].name == "UDATA");
    CHECK((*root)[0].directory);
    const auto title = volume->list(named(*volume->list(named(*root, "UDATA")), "5454009b"));
    REQUIRE(title.has_value());
    REQUIRE(title->size() == 2);
    const FatxEntry save = named(*title, "AB");
    const auto files = volume->list(save);
    REQUIRE(files.has_value());
    const FatxEntry data = named(*files, "profile.dat");
    CHECK(data.size == 40000);
    const auto bytes = volume->read(data, 64 * 1024);
    REQUIRE(bytes.has_value());
    CHECK(*bytes == big);
    const auto refused = volume->read(data, 1000);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::Invalid);
}

TEST_CASE("a FATX volume of 0xfff0 clusters or more has 32-bit FAT entries", "[fatx]") {
    // Expected: save.md#xbox-containers, confirmed (runtime), run r-20261119-xbox-hdd-fatx-layout (the data partition's
    // 313,280 clusters have 4-byte entries)
    const std::uint64_t size = std::uint64_t{0x10000} * 16384;
    const auto pieces = buildFatx(size, {{"UDATA/f", pattern(20, 3)}});
    coney::test::SparseStream stream(pieces, size);
    auto volume = FatxVolume::open(stream, 0, size);
    REQUIRE(volume.has_value());
    CHECK(volume->fatEntryBytes() == 4);
    const auto udata = volume->root();
    REQUIRE(udata.has_value());
    const auto files = volume->list(named(*udata, "UDATA"));
    REQUIRE(files.has_value());
    const auto bytes = volume->read(named(*files, "f"), 100);
    REQUIRE(bytes.has_value());
    CHECK(*bytes == pattern(20, 3));
}

TEST_CASE("a FATX volume with a damaged chain or no magic is refused", "[fatx]") {
    // Expected: save.md#xbox-containers, inferred (a chain that loops, meets a free cluster or leaves the volume is
    // damaged)
    auto image = flatten(buildFatx(1024 * 1024, {{"f", pattern(40000, 1)}}), 1024 * 1024);
    SECTION("a free cluster in the middle of the file") {
        // f starts at cluster 2 (the root takes 1); its entry for cluster 3 is the FAT's bytes 6-7.
        image[0x1000 + 3 * 2] = std::byte{0};
        image[0x1000 + 3 * 2 + 1] = std::byte{0};
    }
    SECTION("a chain that loops") {
        image[0x1000 + 3 * 2] = std::byte{2};
        image[0x1000 + 3 * 2 + 1] = std::byte{0};
    }
    SECTION("a chain that leaves the volume") {
        image[0x1000 + 3 * 2] = std::byte{0x00};
        image[0x1000 + 3 * 2 + 1] = std::byte{0x10};
    }
    MemoryStream stream(image);
    auto volume = FatxVolume::open(stream, 0, image.size());
    REQUIRE(volume.has_value());
    const auto root = volume->root();
    REQUIRE(root.has_value());
    const auto bytes = volume->read(named(*root, "f"), 64 * 1024);
    REQUIRE_FALSE(bytes.has_value());
    CHECK(bytes.error().code == ErrorCode::Invalid);

    std::vector<std::byte> other(8192, std::byte{0});
    MemoryStream none(other);
    CHECK_FALSE(FatxVolume::looksLikeFatx(none, 0));
    const auto refused = FatxVolume::open(none, 0, other.size());
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::Invalid);
}

TEST_CASE("a FATX volume that ends inside its FAT is refused", "[fatx]") {
    // Expected: save.md#xbox-containers, inferred (the FAT follows the 4 KB superblock, 2 bytes a cluster rounded up to
    // 4 KB, so a 1 MB volume of 16 KB clusters has its data from 0x2000)
    auto image = flatten(buildFatx(1024 * 1024, {{"f", pattern(100, 1)}}), 1024 * 1024);
    image.resize(0x1000 + 10);
    MemoryStream stream(image);
    const auto volume = FatxVolume::open(stream, 0, 1024 * 1024);
    REQUIRE_FALSE(volume.has_value());
    CHECK(volume.error().code == ErrorCode::Truncated);
}

TEST_CASE("a FATX folder whose chain is longer than any folder is refused", "[fatx]") {
    // Expected: save.md#xbox-containers, Coney's choice (a folder is read to at most 256 KB, 4,096 entries; a longer
    // chain is damage, and following it would cost memory without bound)
    auto image = flatten(buildFatx(1024 * 1024, {}), 1024 * 1024);
    // Links the root folder (cluster 1) through `clusters` clusters of 16 KB: FAT entry c is the FAT's bytes 2c-2c+1.
    const auto link = [&image](std::uint32_t clusters) {
        for (std::uint32_t c = 1; c <= clusters; ++c) {
            const std::uint32_t next = c == clusters ? 0xffffU : c + 1;
            image[0x1000 + std::size_t{c} * 2] = static_cast<std::byte>(next & 0xffU);
            image[0x1000 + std::size_t{c} * 2 + 1] = static_cast<std::byte>(next >> 8U);
        }
    };
    MemoryStream stream(image);
    SECTION("16 clusters, 256 KB, is a folder") {
        link(16);
        auto volume = FatxVolume::open(stream, 0, image.size());
        REQUIRE(volume.has_value());
        CHECK(volume->root().has_value());
    }
    SECTION("17 clusters is not") {
        link(17);
        auto volume = FatxVolume::open(stream, 0, image.size());
        REQUIRE(volume.has_value());
        const auto root = volume->root();
        REQUIRE_FALSE(root.has_value());
        CHECK(root.error().code == ErrorCode::Invalid);
    }
}
// NOLINTEND(bugprone-implicit-widening-of-multiplication-result)
