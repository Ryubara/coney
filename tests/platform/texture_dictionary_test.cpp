// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/texture_dictionary.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/chunk_types.h"
#include "core/error.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/stream.h"
#include "fileio/wad.h"
#include "graphics/rw_stream.h"
#include "platform/render_engine.h"
#include "support/fixtures.h"
#include "support/rw_fixtures.h"

// These tests run librw on its NULL device (RenderBackend::Null): no window, no GPU, as on CI.

using coney::ErrorCode;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::TextureDictionary;
using coney::test::Bytes;
using coney::test::rgbaTexture2x2;
using coney::test::texDictionary;

namespace {

// Starts the headless renderer, which every test here needs for librw.
std::unique_ptr<RenderEngine> startHeadless() {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// A flat chunk container holding `dictionary` in one chunk of `type`, after a raw Level Header chunk.
Bytes containerWithDictionary(std::uint32_t type, const Bytes& dictionary) {
    Bytes container;
    const auto size = static_cast<std::uint32_t>(dictionary.size());
    container.header(2, 16 + size, 0, 0);
    container.header(0x17, 16, 0, 0).fill(16, 1);
    container.header(type, size, 0, 0xABCD).append(dictionary.span());
    return container;
}

} // namespace

TEST_CASE("a synthetic PS2 texture dictionary reads into RGBA images", "[texture_dictionary]") {
    auto engine = startHeadless();
    const Bytes stream = texDictionary({rgbaTexture2x2("first"), rgbaTexture2x2("second")});
    auto dictionary = TextureDictionary::read(stream.span());
    REQUIRE(dictionary.has_value());
    CHECK(dictionary->textures().size() == 2);
    CHECK(dictionary->info().textures.size() == 2);

    auto images = dictionary->toImages();
    REQUIRE(images.has_value());
    REQUIRE(images->size() == 2);
    // The stream's order is kept.
    CHECK((*images)[0].name == "first");
    CHECK((*images)[1].name == "second");
    const auto& image = (*images)[0];
    CHECK(image.width == 2);
    CHECK(image.height == 2);
    // Red, green / blue, white. PS2 alpha 0x80 (opaque) becomes 255 and 0x40 becomes 127: librw scales alpha by
    // 255/128 when it leaves the PS2's range (the open question in docs/research/graphics.md).
    const std::vector<std::uint8_t> expected{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 127};
    CHECK(image.pixels == expected);
}

TEST_CASE("a damaged dictionary is refused before it reaches librw", "[texture_dictionary]") {
    auto engine = startHeadless();
    const Bytes whole = texDictionary({rgbaTexture2x2("t")});
    auto dictionary = TextureDictionary::read(whole.span().first(whole.size() - 1));
    REQUIRE_FALSE(dictionary.has_value());
    CHECK(dictionary.error().code == ErrorCode::Truncated);
}

TEST_CASE("chunks 0x0B and 0x2A read their dictionary and push it as 0x0B", "[texture_dictionary]") {
    auto engine = startHeadless();
    coney::chunk::ChunkHandlerTable table;
    coney::platform::addTextureDictionaryHandlers(table);

    for (const std::uint32_t type : {coney::chunk::kTextureDictionaryTid, coney::chunk::kRenderwareTextureDic}) {
        // A struct section before the dictionary, which the reader must skip to find it.
        Bytes data;
        data.u32(coney::graphics::kRwStruct).u32(4).u32(coney::graphics::kRwLibraryStamp).u32(0);
        data.append(texDictionary({rgbaTexture2x2("t")}).span());
        const Bytes container = containerWithDictionary(type, data);
        coney::io::MemoryStream stream(container.span());
        coney::chunk::ChunkStacks stacks;
        auto report = coney::chunk::loadContainer(stream, table, stacks);
        REQUIRE(report.has_value());
        CHECK(report->chunks[1].readByHandler);

        auto taken = stacks.takeChunks(coney::chunk::kTextureDictionaryTid);
        REQUIRE(taken.size() == 1);
        CHECK(taken[0].id == 0xABCD);
        auto* object = dynamic_cast<coney::platform::TextureDictionaryObject*>(taken[0].object.get());
        REQUIRE(object != nullptr);
        CHECK(object->describe() == "texture dictionary");
        CHECK(object->dictionary().textures().size() == 1);
        // The raw Level Header is still there.
        CHECK(stacks.chunks().size() == 1);
    }
}

TEST_CASE("a damaged texture chunk fails the load", "[texture_dictionary]") {
    auto engine = startHeadless();
    coney::chunk::ChunkHandlerTable table;
    coney::platform::addTextureDictionaryHandlers(table);
    Bytes garbage;
    garbage.fill(32, 0x55);
    const Bytes container = containerWithDictionary(coney::chunk::kRenderwareTextureDic, garbage);
    coney::io::MemoryStream stream(container.span());
    coney::chunk::ChunkStacks stacks;
    CHECK_FALSE(coney::chunk::loadContainer(stream, table, stacks).has_value());
}

TEST_CASE("loadTextureDictionaries reads headered streams and chunk containers", "[texture_dictionary]") {
    auto engine = startHeadless();
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(table);

    // Five entries: a headered stream starting with a dictionary (as a sector atomics file does), a container with
    // two dictionaries, a container with none, text, and a world stream (a count, then a dictionary).
    Bytes headered;
    headered.header(1, 0, 0, 0x1234).append(texDictionary({rgbaTexture2x2("a"), rgbaTexture2x2("b")}).span());
    const Bytes dictionary = texDictionary({rgbaTexture2x2("c")});
    const auto size = static_cast<std::uint32_t>(dictionary.size());
    Bytes two;
    two.header(2, 2 * size, 0, 0);
    two.header(0x2A, size, 0, 0).append(dictionary.span());
    two.header(0x0B, size, 0, 0).append(dictionary.span());
    Bytes none;
    none.header(1, 16, 0, 0).header(0x17, 16, 0, 0).fill(16, 0);
    Bytes text;
    text.text("-- lua\n");
    Bytes world;
    world.u32(2).append(texDictionary({rgbaTexture2x2("w")}).span());

    Bytes wad;
    Bytes dir;
    dir.header(5, 0, 0, 0);
    for (const auto& [name, data] : {std::pair{"a.sec", &headered}, std::pair{"b.pak", &two}, std::pair{"c.lev", &none},
                                     std::pair{"d.lua", &text}, std::pair{"e.wld", &world}}) {
        dir.u32(static_cast<std::uint32_t>(wad.size()))
            .u32(static_cast<std::uint32_t>(data->size()))
            .u32(coney::nameHash(std::string("./ee_files/") + name));
        wad.append(data->span());
        wad.padTo((wad.size() + 2047) / 2048 * 2048);
    }
    coney::test::TempDir folder;
    folder.write("WARRIORS.DIR", dir.span());
    folder.write("WARRIORS.WAD", wad.span());
    auto disc = coney::io::Disc::open(folder.path());
    REQUIRE(disc.has_value());
    auto opened = coney::io::Wad::open(std::move(*disc));
    REQUIRE(opened.has_value());
    const coney::io::Wad& archive = *opened;

    auto fromHeadered = coney::platform::loadTextureDictionaries(archive, **archive.lookup("a.sec"), table);
    REQUIRE(fromHeadered.has_value());
    REQUIRE(fromHeadered->size() == 1);
    CHECK((*fromHeadered)[0].textures().size() == 2);

    auto fromContainer = coney::platform::loadTextureDictionaries(archive, **archive.lookup("b.pak"), table);
    REQUIRE(fromContainer.has_value());
    CHECK(fromContainer->size() == 2);

    auto fromNone = coney::platform::loadTextureDictionaries(archive, **archive.lookup("c.lev"), table);
    REQUIRE_FALSE(fromNone.has_value());
    CHECK(fromNone.error().code == ErrorCode::NotFound);

    CHECK_FALSE(coney::platform::loadTextureDictionaries(archive, **archive.lookup("d.lua"), table).has_value());

    auto fromWorld = coney::platform::loadTextureDictionaries(archive, **archive.lookup("e.wld"), table);
    REQUIRE(fromWorld.has_value());
    REQUIRE(fromWorld->size() == 1);
    CHECK((*fromWorld)[0].textures().size() == 1);
}

TEST_CASE("the headless renderer has no window and draws nothing", "[render_engine]") {
    auto engine = startHeadless();
    CHECK(engine->backend() == RenderBackend::Null);
    CHECK_FALSE(engine->drawsPixels());
    CHECK_FALSE(engine->window().has_value());
    engine->requestCapture(0, "unused.png");
    engine->beginFrame({});
    engine->present();
    const auto& capture = engine->capture();
    REQUIRE(capture.has_value());
    // An `if` clang-tidy can follow: it does not know that REQUIRE stops the test.
    if (capture) {
        CHECK_FALSE(capture->has_value());
    }
}

TEST_CASE("only one renderer runs at a time and another can start after it", "[render_engine]") {
    {
        auto first = startHeadless();
        auto second = RenderEngine::start(RenderBackend::Null, {});
        REQUIRE_FALSE(second.has_value());
        CHECK(second.error().code == ErrorCode::PlatformFailure);
    }
    auto again = RenderEngine::start(RenderBackend::Null, {});
    CHECK(again.has_value());
}
