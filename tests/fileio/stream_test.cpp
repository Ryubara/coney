// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/stream.h"

#include <array>
#include <cstddef>

#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"

using coney::ErrorCode;
using coney::io::MemoryStream;
using coney::io::SubStream;
using coney::test::Bytes;

TEST_CASE("MemoryStream reads exactly or not at all", "[stream]") {
    Bytes bytes;
    bytes.u32(0xAABBCCDD).u32(0x11223344);
    MemoryStream stream(bytes.span());
    CHECK(stream.size() == 8);
    CHECK(stream.readU32Le() == 0xAABBCCDDU);
    std::array<std::byte, 5> tooMany{};
    auto failed = stream.read(tooMany);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == ErrorCode::Truncated);
    CHECK(stream.tell() == 4);
    CHECK(stream.skip(4).has_value());
    CHECK(stream.remaining() == 0);
    CHECK_FALSE(stream.skip(1).has_value());
    CHECK_FALSE(stream.seek(9).has_value());
}

TEST_CASE("SubStream is a window that cannot read past its end", "[stream]") {
    Bytes bytes;
    bytes.u32(1).u32(2).u32(3);
    MemoryStream parent(bytes.span());
    REQUIRE(parent.skip(4).has_value());
    auto window = SubStream::open(parent, 4);
    REQUIRE(window.has_value());
    CHECK(window->size() == 4);
    CHECK(window->readU32Le() == 2U);
    CHECK_FALSE(window->readU32Le().has_value());
    CHECK_FALSE(SubStream::open(parent, 100).has_value());
}
