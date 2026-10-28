// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_archive.h"

#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/cpak_fixtures.h"

using coney::ErrorCode;
using coney::cpak::Archive;
using coney::test::cpak::buildArchive;
using coney::test::cpak::bytesOf;
using coney::test::cpak::failure;
using coney::test::cpak::rawEntry;

TEST_CASE("an entry is read in place, as a view into the archive's own bytes", "[cpak]") {
    // Expected: ../guides/cpak-format.md#alignment, Coney's own format (entries readable in place)
    const auto bytes = buildArchive({rawEntry("a/one", 208, "first"), rawEntry("b/two", 224, "second!")});
    const auto archive = Archive::open(bytes);
    REQUIRE(archive.has_value());
    const auto view = archive->read("b/two");
    REQUIRE(view.has_value());
    // The same memory, not a copy.
    CHECK(view->data() == bytes.data() + 224);
    CHECK(view->size() == 7);
    CHECK(std::vector<std::byte>(view->begin(), view->end()) == bytesOf("second!"));
}

TEST_CASE("a damaged entry is refused by verified() and read(), and view() leaves the check to its caller", "[cpak]") {
    // Expected: ../guides/cpak-format.md#hashes, Coney's own format
    auto bytes = buildArchive({rawEntry("a/one", 208, "first")});
    bytes[210] = std::byte{'R'};
    const auto archive = Archive::open(bytes);
    REQUIRE(archive.has_value());
    const auto& entry = archive->index().entries()[0];
    CHECK(archive->view(entry).has_value());
    CHECK(failure(archive->verified(entry)) == ErrorCode::Invalid);
    CHECK(failure(archive->read("a/one")) == ErrorCode::Invalid);
}

TEST_CASE("an archive whose bytes are not the size its header gives is refused", "[cpak]") {
    // Expected: ../guides/cpak-format.md#header, Coney's own format
    auto bytes = buildArchive({rawEntry("a/one", 208, "first")});
    auto shorter = bytes;
    shorter.pop_back();
    CHECK(failure(Archive::open(shorter)) == ErrorCode::Truncated);
    bytes.push_back(std::byte{0});
    CHECK(failure(Archive::open(bytes)) == ErrorCode::Invalid);
}

TEST_CASE("reading a name the archive does not hold is NotFound", "[cpak]") {
    // Expected: ../guides/cpak-format.md#reading, Coney's own format
    const auto bytes = buildArchive({rawEntry("a/one", 208, "first")});
    const auto archive = Archive::open(bytes);
    REQUIRE(archive.has_value());
    CHECK(failure(archive->read("a/two")) == ErrorCode::NotFound);
}
