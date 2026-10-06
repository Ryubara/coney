// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/object_list.h"

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::test::Bytes;
using coney::world_objects::ObjectList;

namespace {

// An Object List chunk with two records: a count, 12 header bytes, then the records' nine words each.
Bytes twoRecords() {
    Bytes chunk;
    chunk.header(2, 0, 0, 0);
    chunk.u32(coney::crc32("dyn_bat")).u32(0).u32(12).u32(13).u32(0).u32(100).u32(150).u32(300).u32(0);
    chunk.u32(coney::crc32("dyn_door")).u32(21).u32(22).u32(23).u32(24).u32(200).u32(250).u32(400).u32(500);
    return chunk;
}

} // namespace

TEST_CASE("the Object List's 36-byte records are read and found by name in any case", "[object_list]") {
    auto list = ObjectList::parse(twoRecords().span());
    REQUIRE(list.has_value());
    REQUIRE(list->records().size() == 2);
    const coney::world_objects::ObjectRecord* bat = list->find("DYN_Bat");
    REQUIRE(bat != nullptr);
    CHECK(bat->modelHash == 12);
    CHECK(bat->texturesHash == 13);
    CHECK(bat->modelSize == 100);
    CHECK(bat->texturesSize == 300);
    const coney::world_objects::ObjectRecord* door = list->find("dyn_door");
    REQUIRE(door != nullptr);
    CHECK(door->variantOf == 21);
    CHECK(door->modelHash == 22);
    CHECK(door->fourthHash == 24);
    CHECK(door->fourthSize == 500);
    CHECK(list->find("dyn_nothing") == nullptr);
}

TEST_CASE("an Object List shorter than its count is refused", "[object_list]") {
    const Bytes chunk = twoRecords();
    auto cut = ObjectList::parse(chunk.span().first(chunk.size() - 4));
    REQUIRE_FALSE(cut.has_value());
    CHECK(cut.error().code == ErrorCode::Truncated);
    CHECK_FALSE(ObjectList::parse({}).has_value());
}
