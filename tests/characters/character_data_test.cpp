// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_data.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "fileio/stream.h"
#include "support/character_fixtures.h"

using coney::ErrorCode;
using coney::test::Bytes;
using coney::test::chunk;
using coney::test::ClipFields;

namespace {

// The keyframe and descriptor chunks of a one-channel clip called `name`.
std::pair<Bytes, Bytes> clipChunks(std::string_view name) {
    const Bytes keys = coney::test::clipKeys({{0, 0, 0, 0}});
    ClipFields fields;
    fields.name = name;
    fields.mask = 2;
    fields.channels = 1;
    return {chunk(coney::anim::kAnimKeyframesChunk, keys),
            chunk(coney::anim::kAnimDataChunk, coney::test::clipDescriptor(fields, 0, 0, keys.size()))};
}

// A Character Data chunk whose anim ids take `slots` in order and the default after them.
Bytes characterData(std::initializer_list<std::uint32_t> slots) {
    Bytes data;
    data.u32(0).u32(0);
    for (const std::uint32_t slot : slots) {
        data.u32(slot);
    }
    while (data.size() < 8 + coney::characters::kAnimIds * 4) {
        data.u32(coney::characters::kDefaultAnimSlot);
    }
    return data.padTo(coney::characters::kCharacterDataBytes);
}

// A character data resource: clips "first", "second", "third" in that order, a range list, then the table.
Bytes resource(std::initializer_list<std::uint32_t> slots) {
    const auto [k1, d1] = clipChunks("first");
    const auto [k2, d2] = clipChunks("second");
    const auto [k3, d3] = clipChunks("third");
    Bytes rangeList;
    rangeList.u32(722).padTo(32);
    return coney::test::container({k1, d1, k2, d2, k3, d3, chunk(coney::characters::kAnimRangeListChunk, rangeList),
                                   chunk(coney::characters::kCharacterDataChunk, characterData(slots))});
}

// Loads `bytes` as a flat container with the character data handlers.
std::expected<coney::chunk::LoadReport, coney::Error> load(const Bytes& bytes, coney::chunk::ChunkStacks& stacks) {
    coney::chunk::ChunkHandlerTable table;
    coney::characters::addCharacterDataHandlers(table);
    coney::io::MemoryStream stream(bytes.span());
    return coney::chunk::loadContainer(stream, table, stacks);
}

} // namespace

TEST_CASE("the anim table takes the clips off the object stack, smallest slot value first", "[character_data]") {
    // Ids 0-4 name slot values 5, 9, 5, 2 and the default. Values 2, 5, 9 take the clips popped last-in first-out:
    // 2 -> third, 5 -> second, 9 -> first.
    coney::chunk::ChunkStacks stacks;
    REQUIRE(load(resource({5, 9, 5, 2, coney::characters::kDefaultAnimSlot}), stacks).has_value());
    auto object = stacks.popObject<coney::characters::CharacterDataObject>();
    REQUIRE(object.has_value());
    const coney::characters::CharacterData& data = (*object)->data();
    REQUIRE(data.clips().size() == 3);
    REQUIRE(data.animation(0) != nullptr);
    CHECK(data.animation(0)->name == "second");
    CHECK(data.animation(1)->name == "first");
    CHECK(data.animation(2) == data.animation(0));
    CHECK(data.animation(3)->name == "third");
    CHECK(data.animation(4) == nullptr);   // the resource manager's default: not resolved
    CHECK(data.animation(722) == nullptr); // out of range
    CHECK(data.findClip("third") == data.animation(3));
    CHECK(data.findClip("fourth") == nullptr);
    CHECK(data.rangeList().size() == 32);
    CHECK(stacks.objects().empty());
}

TEST_CASE("an anim table that names more clips than were loaded, or a slot past the ids, fails", "[character_data]") {
    coney::chunk::ChunkStacks stacks;
    auto tooMany = load(resource({0, 1, 2, 3}), stacks);
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().code == ErrorCode::Invalid);

    coney::chunk::ChunkStacks other;
    auto beyond = load(resource({722}), other);
    REQUIRE_FALSE(beyond.has_value());
    CHECK(beyond.error().code == ErrorCode::Invalid);
}
