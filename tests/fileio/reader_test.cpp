// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/reader.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"

using coney::ErrorCode;
using coney::io::Reader;
using coney::test::Bytes;

TEST_CASE("Reader reads little-endian values and stops at the end", "[reader]") {
    Bytes bytes;
    bytes.u8(0x01).u16(0x0302).u32(0x07060504).u32(0x3F800000); // 0x3F800000 is 1.0 as an IEEE 754 single
    Reader reader(bytes.span());
    CHECK(reader.readU8() == 0x01);
    CHECK(reader.readU16Le() == 0x0302);
    CHECK(reader.readU32Le() == 0x07060504U);
    CHECK(reader.readF32Le() == 1.0F);
    CHECK(reader.remaining() == 0);
    auto past = reader.readU8();
    REQUIRE_FALSE(past.has_value());
    CHECK(past.error().code == ErrorCode::Truncated);
}

TEST_CASE("Reader leaves its position alone after a failed read", "[reader]") {
    Bytes bytes;
    bytes.u8(1).u8(2).u8(3);
    Reader reader(bytes.span());
    CHECK_FALSE(reader.readU32Le().has_value());
    CHECK(reader.position() == 0);
    CHECK(reader.seek(3).has_value());
    CHECK_FALSE(reader.seek(4).has_value());
    CHECK(reader.position() == 3);
}
