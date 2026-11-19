// SPDX-License-Identifier: GPL-3.0-or-later
// The qcow2 reader, on synthetic images built by the tests (docs/research/save.md#xbox-containers).
#include "fileio/qcow2_image.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fileio/stream.h"
#include "support/xbox_save_fixtures.h"

using coney::ErrorCode;
using coney::io::MemoryStream;
using coney::io::Qcow2Image;
using coney::test::buildQcow2;
using coney::test::Qcow2Options;

namespace {

// `count` bytes counting up from `start`.
std::vector<std::byte> pattern(std::size_t count, std::uint8_t start) {
    std::vector<std::byte> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<std::byte>(static_cast<std::uint8_t>(start + i));
    }
    return out;
}

constexpr std::uint64_t kGigabyte = std::uint64_t{1} << 30;

} // namespace

TEST_CASE("a qcow2 image reads its written clusters, and zeros where nothing was written", "[qcow2]") {
    // Expected: save.md#xbox-containers, confirmed (runtime), run r-20261119-xbox-hdd-fatx-layout (xemu's image:
    // version 3, 64 KB clusters, an 8 GB disk), the cluster tables inferred from the format
    const auto across = pattern(70000, 5); // over a cluster's end
    const std::uint64_t far = 3 * kGigabyte + 12345;
    const auto image = buildQcow2(8 * kGigabyte, {{100, across}, {far, pattern(10, 200)}});
    MemoryStream file(image);
    CHECK(Qcow2Image::looksLikeQcow2(file));
    auto disk = Qcow2Image::open(file);
    REQUIRE(disk.has_value());
    CHECK(disk->size() == 8 * kGigabyte);
    CHECK(disk->clusterBytes() == 65536);

    std::vector<std::byte> got(70000);
    REQUIRE(disk->seek(100).has_value());
    REQUIRE(disk->read(got).has_value());
    CHECK(got == across);
    std::vector<std::byte> ten(10);
    REQUIRE(disk->seek(far).has_value());
    REQUIRE(disk->read(ten).has_value());
    CHECK(ten == pattern(10, 200));
    std::vector<std::byte> zeros(4096, std::byte{1});
    REQUIRE(disk->seek(5 * kGigabyte).has_value());
    REQUIRE(disk->read(zeros).has_value());
    CHECK(zeros == std::vector<std::byte>(4096, std::byte{0}));
    REQUIRE(disk->seek(8 * kGigabyte - 2).has_value());
    CHECK(disk->read(ten).error().code == ErrorCode::Truncated);
}

TEST_CASE("a qcow2 image of version 2 reads the same", "[qcow2]") {
    // Expected: save.md#xbox-containers, inferred (version 2 has the same tables, without the version 3 header fields)
    Qcow2Options options;
    options.version = 2;
    const auto image = buildQcow2(kGigabyte, {{4096, pattern(16, 1)}}, options);
    MemoryStream file(image);
    auto disk = Qcow2Image::open(file);
    REQUIRE(disk.has_value());
    std::vector<std::byte> got(16);
    REQUIRE(disk->seek(4096).has_value());
    REQUIRE(disk->read(got).has_value());
    CHECK(got == pattern(16, 1));
}

TEST_CASE("a qcow2 image Coney cannot read is refused, and a compressed cluster fails its read", "[qcow2]") {
    // Expected: save.md#xbox-containers, inferred (Coney's reader leaves these features out)
    Qcow2Options options;
    SECTION("a backing file") { options.backingFile = true; }
    SECTION("encryption") { options.cryptMethod = 1; }
    SECTION("an external data file") { options.incompatible = 1U << 2U; }
    SECTION("the corrupt mark") { options.incompatible = 1U << 1U; }
    SECTION("version 4") { options.version = 4; }
    const auto image = buildQcow2(kGigabyte, {{0, pattern(16, 1)}}, options);
    MemoryStream file(image);
    const auto disk = Qcow2Image::open(file);
    REQUIRE_FALSE(disk.has_value());
    CHECK(disk.error().code == ErrorCode::Invalid);

    Qcow2Options compressed;
    compressed.compressFirst = true;
    const auto packed = buildQcow2(kGigabyte, {{0, pattern(16, 1)}}, compressed);
    MemoryStream packedFile(packed);
    auto packedDisk = Qcow2Image::open(packedFile);
    REQUIRE(packedDisk.has_value());
    std::vector<std::byte> got(16);
    const auto read = packedDisk->read(got);
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().code == ErrorCode::Invalid);
    CHECK(read.error().message.find("compressed") != std::string::npos);

    std::vector<std::byte> other(512, std::byte{0});
    MemoryStream none(other);
    CHECK_FALSE(Qcow2Image::looksLikeQcow2(none));
}

TEST_CASE("a qcow2 image cut short in its header or its L1 table, or with too large an L1 table, is refused",
          "[qcow2]") {
    // Expected: save.md#xbox-containers, inferred (the header is 72 bytes in version 2 and 104 in version 3; the L1
    // table's entry count is the header's word at 36 and its offset the word at 40, big-endian; Coney reads at most
    // 2^22 entries)
    auto image = buildQcow2(kGigabyte, {{0, pattern(16, 1)}});
    // Writes `value` big-endian into `width` bytes at `offset` of the header.
    const auto put = [&image](std::size_t offset, std::size_t width, std::uint64_t value) {
        for (std::size_t i = 0; i < width; ++i) {
            image[offset + i] = static_cast<std::byte>((value >> (8 * (width - 1 - i))) & 0xffU);
        }
    };
    SECTION("shorter than a version 2 header") { image.resize(50); }
    SECTION("a version 3 image shorter than its header") { image.resize(80); }
    SECTION("an L1 table past the end of the file") { put(40, 8, image.size()); }
    SECTION("an L1 table running past the end of the file") { put(40, 8, image.size() - 4); }
    SECTION("more L1 entries than Coney reads") { put(36, 4, (std::uint64_t{1} << 22U) + 1); }
    MemoryStream file(image);
    const auto disk = Qcow2Image::open(file);
    REQUIRE_FALSE(disk.has_value());
    CHECK((disk.error().code == ErrorCode::Truncated || disk.error().code == ErrorCode::Invalid));
}
