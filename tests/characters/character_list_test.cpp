// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_list.h"

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::characters::CharacterList;
using coney::test::Bytes;

namespace {

// A Character List chunk with two records: a count, 12 header bytes, then the records' eight words each.
Bytes twoRecords() {
    Bytes chunk;
    chunk.header(2, 0, 0, 0);
    chunk.u32(coney::crc32("hero_cv")).u32(11).u32(12).u32(13).u32(100).u32(200).u32(201).u32(300);
    chunk.u32(coney::crc32("villain")).u32(21).u32(22).u32(23).u32(0).u32(0).u32(0).u32(0);
    return chunk;
}

} // namespace

TEST_CASE("the Character List's records are read and found by model name in any case", "[character_list]") {
    auto list = CharacterList::parse(twoRecords().span());
    REQUIRE(list.has_value());
    REQUIRE(list->records().size() == 2);
    const coney::characters::CharacterRecord* hero = list->find("HERO_cv");
    REQUIRE(hero != nullptr);
    CHECK(hero->dataHash == 11);
    CHECK(hero->modelHash == 12);
    CHECK(hero->texturesHash == 13);
    CHECK(hero->texturesSize == 300);
    CHECK(list->find("villain")->modelHash == 22);
    CHECK(list->find("nobody") == nullptr);
    CHECK(coney::characters::resourceFileName(3676172086U) == "3676172086");
}

TEST_CASE("a Character List shorter than its count is refused", "[character_list]") {
    const Bytes chunk = twoRecords();
    auto cut = CharacterList::parse(chunk.span().first(chunk.size() - 4));
    REQUIRE_FALSE(cut.has_value());
    CHECK(cut.error().code == ErrorCode::Truncated);
    auto empty = CharacterList::parse({});
    REQUIRE_FALSE(empty.has_value());
}
