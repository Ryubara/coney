// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/particle_page.h"

#include <cstdint>
#include <initializer_list>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/stream.h"
#include "support/fixtures.h"
#include "support/sheet_fixtures.h"

using coney::ErrorCode;
using coney::graphics::parseParticlePage;
using coney::graphics::UvRect;
using coney::test::Bytes;
using coney::test::particlePage;

TEST_CASE("a particle page reads its rectangles and first glyph", "[particle_page]") {
    const Bytes data = particlePage(-1, {UvRect{0.0F, 0.0F, 0.5F, 0.75F}, UvRect{0.5F, 0.25F, 1.0F, 1.0F}});
    CHECK(data.size() == 64); // 0x14 + 2 * 16 = 52, padded to 64
    auto parsed = parseParticlePage(data.span());
    REQUIRE(parsed.has_value());
    CHECK(parsed->firstGlyph == -1);
    REQUIRE(parsed->rects.size() == 2);
    CHECK(parsed->rect(0) == UvRect{0.0F, 0.0F, 0.5F, 0.75F});
    CHECK(parsed->rect(1) == UvRect{0.5F, 0.25F, 1.0F, 1.0F});
}

TEST_CASE("a font page keeps its first glyph", "[particle_page]") {
    auto parsed = parseParticlePage(particlePage(94, {UvRect{}}).span());
    REQUIRE(parsed.has_value());
    CHECK(parsed->firstGlyph == 94);
}

TEST_CASE("a particle page shorter than its header or its count is refused", "[particle_page]") {
    Bytes header;
    header.u32(0).u32(1).u32(0);
    auto shortHeader = parseParticlePage(header.span());
    REQUIRE_FALSE(shortHeader.has_value());
    CHECK(shortHeader.error().code == ErrorCode::Truncated);

    // A count of three with room for two.
    Bytes data = particlePage(-1, {UvRect{}, UvRect{}});
    data.patchU32(4, 3);
    auto tooMany = parseParticlePage(data.span().first(0x14 + 2 * 16));
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().code == ErrorCode::Truncated);

    // A huge count is refused before anything is allocated.
    data.patchU32(4, 0xFFFFFFFF);
    CHECK_FALSE(parseParticlePage(data.span()).has_value());
}

namespace {

// A synthetic 0x4D chunk: the count, then {size, name hash} per record.
Bytes sheetTable(std::initializer_list<coney::graphics::SheetTableRecord> records) {
    Bytes bytes;
    bytes.u32(static_cast<std::uint32_t>(records.size()));
    for (const coney::graphics::SheetTableRecord& record : records) {
        bytes.u32(record.size).u32(record.nameHash);
    }
    return bytes;
}

} // namespace

TEST_CASE("the sprite sheet table maps an index or a name hash to a sheet", "[particle_page]") {
    const Bytes data = sheetTable({{1000, 0xAAAA}, {2000, 0xBBBB}, {3000, 0xCCCC}});
    auto table = coney::graphics::parseSpriteSheetTable(data.span());
    REQUIRE(table.has_value());
    REQUIRE(table->records.size() == 3);
    CHECK(table->record(1) == coney::graphics::SheetTableRecord{2000, 0xBBBB});
    CHECK(table->sizeOf(0xCCCC) == std::optional<std::uint32_t>{3000});
    CHECK_FALSE(table->sizeOf(0xDDDD).has_value());

    // A count larger than the records present is refused.
    auto truncated = coney::graphics::parseSpriteSheetTable(data.span().first(data.size() - 1));
    REQUIRE_FALSE(truncated.has_value());
    CHECK(truncated.error().code == ErrorCode::Truncated);
    CHECK_FALSE(coney::graphics::parseSpriteSheetTable(data.span().first(2)).has_value());
}

TEST_CASE("chunk 0x4D is replaced on the stack by the sprite sheet table", "[particle_page]") {
    coney::chunk::ChunkHandlerTable handlers;
    handlers.setHandlers(coney::graphics::kParticlePageHeader,
                         coney::chunk::ChunkHandlers{coney::graphics::onParticlePageHeaderLoaded, {}});
    const Bytes table = sheetTable({{16, 0x1234}});
    Bytes container;
    container.header(1, static_cast<std::uint32_t>(table.size()), 0, 0);
    container.header(coney::graphics::kParticlePageHeader, static_cast<std::uint32_t>(table.size()), 0, 7);
    container.append(table.span());
    coney::io::MemoryStream stream(container.span());
    coney::chunk::ChunkStacks stacks;
    REQUIRE(coney::chunk::loadContainer(stream, handlers, stacks).has_value());
    auto taken = stacks.takeChunks(coney::graphics::kParticlePageHeader);
    REQUIRE(taken.size() == 1);
    const auto* object = dynamic_cast<const coney::graphics::SpriteSheetTableObject*>(taken[0].object.get());
    REQUIRE(object != nullptr);
    CHECK(object->describe() == "sprite sheet table");
    CHECK(object->table().records.size() == 1);
    CHECK(object->table().record(0).nameHash == 0x1234);
}
