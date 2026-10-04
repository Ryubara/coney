// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sprite_sheets.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/chunk_types.h"
#include "core/error.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/stream.h"
#include "fileio/wad.h"
#include "gamemodes/legal_screen_mode.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "support/fixtures.h"
#include "support/rw_fixtures.h"
#include "support/sheet_fixtures.h"

// These tests run librw on its NULL device: no window, no GPU, as on CI.

using coney::ErrorCode;
using coney::graphics::UvRect;
using coney::platform::kParticlePage;
using coney::platform::SpriteSheetObject;
using coney::test::Bytes;

namespace {

// Starts the headless renderer, which librw's texture reading needs.
std::unique_ptr<coney::platform::RenderEngine> startHeadless() {
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// The handlers a sprite sheet needs: the texture dictionary readers and the 0x4C handler.
coney::chunk::ChunkHandlerTable sheetHandlers() {
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(table);
    coney::platform::addSpriteSheetHandlers(table);
    return table;
}

// A flat container holding one sprite sheet as the disc does: a 0x2A texture dictionary, then the 0x4C page.
Bytes sheetContainer(const Bytes& dictionary, const Bytes& page) {
    Bytes container;
    const auto dictionarySize = static_cast<std::uint32_t>(dictionary.size());
    const auto pageSize = static_cast<std::uint32_t>(page.size());
    container.header(2, dictionarySize + pageSize, 0, 0);
    container.header(coney::chunk::kRenderwareTextureDic, dictionarySize, 0, 0).append(dictionary.span());
    container.header(kParticlePage, pageSize, 0, 0x4C4C).append(page.span());
    return container;
}

} // namespace

TEST_CASE("a particle page is bound to the texture of the dictionary before it", "[sprite_sheets]") {
    auto engine = startHeadless();
    const auto table = sheetHandlers();
    const Bytes container =
        sheetContainer(coney::test::texDictionary({coney::test::rgbaTexture2x2("sheet")}),
                       coney::test::particlePage(-1, {UvRect{0.0F, 0.0F, 0.5F, 0.5F}, UvRect{0.5F, 0.5F, 1.0F, 1.0F}}));
    coney::io::MemoryStream stream(container.span());
    coney::chunk::ChunkStacks stacks;
    REQUIRE(coney::chunk::loadContainer(stream, table, stacks).has_value());

    // Both chunks became one sprite sheet under 0x4C; the dictionary is no longer on the stack.
    REQUIRE(stacks.chunks().size() == 1);
    auto taken = stacks.takeChunks(kParticlePage);
    REQUIRE(taken.size() == 1);
    CHECK(taken[0].id == 0x4C4C);
    const auto* sheet = dynamic_cast<const SpriteSheetObject*>(taken[0].object.get());
    REQUIRE(sheet != nullptr);
    CHECK(sheet->describe() == "sprite sheet");
    CHECK(sheet->page().rects.size() == 2);
    CHECK(sheet->page().rect(1) == UvRect{0.5F, 0.5F, 1.0F, 1.0F});
    CHECK(sheet->texture()->width() == 2);
    CHECK(sheet->texture()->height() == 2);
    const coney::graphics::SpriteSheet shared = sheet->sheet();
    CHECK(shared.texture.get() == sheet->texture().get());
}

TEST_CASE("a particle page without a texture dictionary before it fails the load", "[sprite_sheets]") {
    auto engine = startHeadless();
    const auto table = sheetHandlers();
    const Bytes page = coney::test::particlePage(-1, {UvRect{}});
    Bytes container;
    container.header(2, 16 + static_cast<std::uint32_t>(page.size()), 0, 0);
    container.header(0x17, 16, 0, 0).fill(16, 0);
    container.header(kParticlePage, static_cast<std::uint32_t>(page.size()), 0, 0).append(page.span());
    coney::io::MemoryStream stream(container.span());
    coney::chunk::ChunkStacks stacks;
    CHECK_FALSE(coney::chunk::loadContainer(stream, table, stacks).has_value());
}

TEST_CASE("a particle page over an empty texture dictionary fails the load", "[sprite_sheets]") {
    auto engine = startHeadless();
    const auto table = sheetHandlers();
    const Bytes container = sheetContainer(coney::test::texDictionary(std::initializer_list<Bytes>{}),
                                           coney::test::particlePage(-1, {UvRect{}}));
    coney::io::MemoryStream stream(container.span());
    coney::chunk::ChunkStacks stacks;
    auto loaded = coney::chunk::loadContainer(stream, table, stacks);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == ErrorCode::Invalid);
}

TEST_CASE("a sprite sheet resource is found by the decimal CRC of its name", "[sprite_sheets]") {
    auto engine = startHeadless();
    const auto table = sheetHandlers();
    const Bytes container = sheetContainer(coney::test::texDictionary({coney::test::rgbaTexture2x2("s")}),
                                           coney::test::particlePage(-1, {UvRect{0.0F, 0.0F, 1.0F, 0.75F}}));
    Bytes dir;
    dir.header(1, 0, 0, 0);
    dir.u32(0).u32(static_cast<std::uint32_t>(container.size()));
    dir.u32(coney::nameHash("./ee_files/" + coney::resourceFileName("test_sheet")));
    coney::test::TempDir folder;
    folder.write("WARRIORS.DIR", dir.span());
    folder.write("WARRIORS.WAD", container.span());
    auto disc = coney::io::Disc::open(folder.path());
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());

    auto sheet = coney::platform::loadSpriteSheetResource(*wad, table, "test_sheet", false);
    REQUIRE(sheet.has_value());
    CHECK(sheet->page.rects.size() == 1);
    REQUIRE(sheet->texture != nullptr);
    CHECK(sheet->texture->width() == 2);

    auto missing = coney::platform::loadSpriteSheetResource(*wad, table, "no_such_sheet", false);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}
