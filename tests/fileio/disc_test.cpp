// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/disc.h"

#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fileio/stream.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::io::Disc;
using coney::test::Bytes;
using coney::test::TempDir;

namespace {

std::vector<std::byte> payload(std::size_t size, std::uint8_t seed) {
    Bytes bytes;
    for (std::size_t i = 0; i < size; ++i) {
        bytes.u8(static_cast<std::uint8_t>(seed + i));
    }
    return bytes.data();
}

} // namespace

TEST_CASE("cleanDiscName drops the version suffix and ignores case", "[disc]") {
    CHECK(coney::io::cleanDiscName("warriors.dir;1") == "WARRIORS.DIR");
    CHECK(coney::io::cleanDiscName("SYSTEM.") == "SYSTEM");
}

TEST_CASE("a disc folder finds root files in any case", "[disc]") {
    TempDir dir;
    dir.write("Warriors.Dir", payload(20, 1));
    auto disc = Disc::open(dir.path());
    REQUIRE(disc.has_value());
    CHECK_FALSE(disc->isImage());
    CHECK(disc->has("WARRIORS.DIR"));
    CHECK(disc->has("warriors.dir;1"));
    CHECK(disc->fileSize("WARRIORS.DIR") == 20U);
    auto data = disc->readFile("warriors.dir");
    REQUIRE(data.has_value());
    CHECK(*data == payload(20, 1));
    auto range = disc->openFileRange("WARRIORS.DIR", 4, 8);
    REQUIRE(range.has_value());
    CHECK(range->readU32Le() == 0x08070605U);
    CHECK_FALSE(disc->openFileRange("WARRIORS.DIR", 16, 8).has_value());
    auto missing = disc->openFile("WARRIORS.WAD");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

TEST_CASE("an ISO image's root directory is read", "[disc]") {
    TempDir dir;
    const auto image = coney::test::buildIso({{"WARRIORS.DIR;1", payload(3000, 7)}, {"OTHER.BIN;1", payload(5, 9)}});
    const auto path = dir.write("game.iso", image);
    auto disc = Disc::open(path);
    REQUIRE(disc.has_value());
    CHECK(disc->isImage());
    CHECK(disc->has("warriors.dir"));
    CHECK(disc->has("OTHER.BIN"));
    CHECK_FALSE(disc->has("SUBDIR")); // directories are not files
    CHECK(disc->readFile("WARRIORS.DIR") == payload(3000, 7));
    CHECK(disc->readFile("OTHER.BIN") == payload(5, 9));
}

TEST_CASE("a file that is not an ISO image is refused", "[disc]") {
    TempDir dir;
    const auto path = dir.write("junk.iso", payload(40000, 3));
    auto disc = Disc::open(path);
    REQUIRE_FALSE(disc.has_value());
    CHECK(disc.error().code == ErrorCode::Invalid);
    auto tiny = Disc::open(dir.write("tiny.iso", payload(10, 3)));
    REQUIRE_FALSE(tiny.has_value());
    CHECK(tiny.error().code == ErrorCode::Invalid);
}

TEST_CASE("a missing path is refused", "[disc]") {
    TempDir dir;
    auto disc = Disc::open(dir.path() / "nothing-here");
    REQUIRE_FALSE(disc.has_value());
    CHECK(disc.error().code == ErrorCode::NotFound);
}

TEST_CASE("an ISO image with a damaged root directory is refused", "[disc]") {
    auto image = Bytes().append(coney::test::buildIso({{"A.BIN;1", payload(4, 1)}}));
    // Make the first directory record claim a length too short to be a record.
    image.patchU32(std::size_t{18} * 2048, 0x00000010);
    coney::io::MemoryStream stream(image.span());
    auto files = coney::io::readIsoRoot(stream);
    REQUIRE_FALSE(files.has_value());
    CHECK(files.error().code == ErrorCode::Invalid);

    // A root directory placed past the end of the image.
    auto cut = Bytes().append(coney::test::buildIso({{"A.BIN;1", payload(4, 1)}}));
    cut.patchU32(16 * 2048 + 156 + 2, 5000);
    coney::io::MemoryStream cutStream(cut.span());
    auto cutFiles = coney::io::readIsoRoot(cutStream);
    REQUIRE_FALSE(cutFiles.has_value());
    CHECK(cutFiles.error().code == ErrorCode::Truncated);
}
