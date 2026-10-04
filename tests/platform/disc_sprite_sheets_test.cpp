// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every sprite sheet (chunk 0x4C) in WARRIORS.WAD loaded through the chunk
// handlers and bound to its texture, and the sheet table (chunk 0x4D) of warriors.glr read, with the counts compared to
// docs/research/gui.md. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise, so
// CI never needs the game. It prints counts only, never data (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "core/chunk_types.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/load_entry_mode.h"
#include "graphics/font.h"
#include "graphics/particle_page.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"

namespace {

// Totals over the whole disc.
struct SheetTotals {
    std::uint64_t entries = 0;            // entries holding at least one 0x4C chunk
    std::uint64_t rawPages = 0;           // 0x4C chunks seen in the raw loads
    std::uint64_t afterDictionary = 0;    // ... directly after a 0x2A chunk
    std::uint64_t exactSize = 0;          // ... whose size is align16(0x14 + 16 * count)
    std::uint64_t sheets = 0;             // sprite sheets the handlers built
    std::uint64_t oneTexture = 0;         // ... whose dictionary holds exactly one texture
    std::uint64_t notFont = 0;            // ... with firstGlyph -1
    std::uint64_t coordinatesInRange = 0; // ... whose every rectangle lies in [0, 1] with u1 >= u0 and v1 >= v0
    std::uint64_t fonts = 0;              // ... with a first glyph, which load as fonts
    std::uint64_t fullFonts = 0;          // ... with a rectangle for every byte from their first glyph on
    std::uint64_t failedEntries = 0;
};

// Whether every rectangle of `page` lies inside the texture, the right way round.
bool rectanglesInRange(const coney::graphics::ParticlePage& page) {
    for (const coney::graphics::UvRect& rect : page.rects) {
        const bool inside = rect.u0 >= 0.0F && rect.v0 >= 0.0F && rect.u1 <= 1.0F && rect.v1 <= 1.0F;
        if (!inside || rect.u1 < rect.u0 || rect.v1 < rect.v0) {
            return false;
        }
    }
    return true;
}

// Counts the raw 0x4C chunks of a load report: how many, how many follow a 0x2A chunk, how many have the exact size.
void countRawPages(const coney::chunk::LoadReport& report, SheetTotals& totals,
                   const std::vector<std::uint32_t>& rectCounts) {
    std::size_t page = 0;
    for (std::size_t i = 0; i < report.chunks.size(); ++i) {
        const coney::chunk::ChunkHeader& header = report.chunks[i].header;
        if (header.type != coney::platform::kParticlePage) {
            continue;
        }
        ++totals.rawPages;
        if (i > 0 && report.chunks[i - 1].header.type == coney::chunk::kRenderwareTextureDic) {
            ++totals.afterDictionary;
        }
        if (page < rectCounts.size() && header.size == (0x14 + 16 * rectCounts[page] + 15) / 16 * 16) {
            ++totals.exactSize;
        }
        ++page;
    }
}

} // namespace

TEST_CASE("every sprite sheet on the disc loads and the sheet table matches", "[disc][sprite_sheets]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto opened = coney::io::Wad::open(std::move(*disc));
    REQUIRE(opened.has_value());
    if (!opened) {
        return;
    }
    const coney::io::Wad& wad = *opened;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const coney::chunk::ChunkHandlerTable rawTable = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(table);
    coney::platform::addSpriteSheetHandlers(table);

    // Every entry: a quick raw load finds the ones with sheets, which are then loaded with the real handlers.
    SheetTotals totals;
    for (const coney::io::WadEntry& entry : wad.index().entries()) {
        auto raw = coney::loadWadEntry(wad, entry, rawTable);
        if (!raw) {
            continue; // not a chunk container
        }
        bool hasPage = false;
        for (const coney::chunk::ChunkRecord& record : raw->report.chunks) {
            hasPage = hasPage || record.header.type == coney::platform::kParticlePage;
        }
        if (!hasPage) {
            continue;
        }
        ++totals.entries;
        auto sheets = coney::platform::loadSpriteSheets(wad, entry, table);
        if (!sheets) {
            ++totals.failedEntries;
            std::printf("  entry %u: %s\n", entry.index, sheets.error().message.c_str());
            continue;
        }
        std::vector<std::uint32_t> rectCounts;
        rectCounts.reserve(sheets->size());
        for (const std::unique_ptr<coney::platform::SpriteSheetObject>& sheet : *sheets) {
            ++totals.sheets;
            rectCounts.push_back(static_cast<std::uint32_t>(sheet->page().rects.size()));
            totals.oneTexture += sheet->texture()->dictionaryTextureCount() == 1 ? 1 : 0;
            totals.notFont += sheet->page().firstGlyph == -1 ? 1 : 0;
            totals.coordinatesInRange += rectanglesInRange(sheet->page()) ? 1 : 0;
            if (sheet->page().firstGlyph >= 0) {
                // Every font sheet must load as a font (docs/research/gui.md#text).
                auto font = coney::graphics::Font::fromSheet(sheet->sheet());
                totals.fonts += font ? 1 : 0;
                totals.fullFonts += font && font->glyph(0xff).has_value() ? 1 : 0;
            }
        }
        countRawPages(raw->report, totals, rectCounts);
    }

    std::printf("entries with sheets: %llu; 0x4C chunks: %llu, after a 0x2A: %llu, exact size: %llu\n",
                static_cast<unsigned long long>(totals.entries), static_cast<unsigned long long>(totals.rawPages),
                static_cast<unsigned long long>(totals.afterDictionary),
                static_cast<unsigned long long>(totals.exactSize));
    std::printf("fonts: %llu, of which %llu cover all 256 bytes\n", static_cast<unsigned long long>(totals.fonts),
                static_cast<unsigned long long>(totals.fullFonts));
    std::printf("sheets loaded: %llu, one texture: %llu, not fonts: %llu, rectangles in range: %llu, failed: %llu\n",
                static_cast<unsigned long long>(totals.sheets), static_cast<unsigned long long>(totals.oneTexture),
                static_cast<unsigned long long>(totals.notFont),
                static_cast<unsigned long long>(totals.coordinatesInRange),
                static_cast<unsigned long long>(totals.failedEntries));
    // The counts of docs/research/gui.md#particle-page.
    CHECK(totals.failedEntries == 0);
    CHECK(totals.rawPages == 1335);
    CHECK(totals.sheets == 1335);
    CHECK(totals.afterDictionary == 1335);
    CHECK(totals.exactSize == 1335);
    CHECK(totals.oneTexture == 1335);
    CHECK(totals.coordinatesInRange == 1335);
    CHECK(totals.notFont == 1328);
    CHECK(totals.fonts == 7);

    // The sheet table in warriors.glr: 576 records, and the sheets the page names at their indices.
    auto glr = wad.lookup("warriors.glr");
    REQUIRE(glr.has_value());
    if (!glr) {
        return;
    }
    auto load = coney::loadWadEntry(wad, **glr, table);
    REQUIRE(load.has_value());
    if (!load) {
        return;
    }
    auto tables = load->stacks.takeChunks(coney::graphics::kParticlePageHeader);
    REQUIRE(tables.size() == 1);
    const auto* object = dynamic_cast<const coney::graphics::SpriteSheetTableObject*>(tables[0].object.get());
    REQUIRE(object != nullptr);
    const coney::graphics::SpriteSheetTable& sheetTable = object->table();
    std::uint64_t named = 0;
    std::uint64_t sizeMatches = 0;
    for (const coney::graphics::SheetTableRecord& record : sheetTable.records) {
        auto file = wad.lookup(std::to_string(record.nameHash));
        if (file) {
            ++named;
            sizeMatches += (*file)->size == record.size ? 1 : 0;
        }
    }
    std::printf("sheet table: %zu records, %llu name a WAD file, %llu of those with the file's size\n",
                sheetTable.records.size(), static_cast<unsigned long long>(named),
                static_cast<unsigned long long>(sizeMatches));
    REQUIRE(sheetTable.records.size() == 576);
    for (const auto& [index, name] : {std::pair<std::size_t, const char*>{0, "part_page0"},
                                      {1, "part_page1"},
                                      {3, "menu_system"},
                                      {7, "part_fire"},
                                      {8, "lighting"},
                                      {10, "hud_minigames"},
                                      {13, "big_font"},
                                      {51, "legal_screen"}}) {
        INFO("record " << index << " is " << name);
        CHECK(sheetTable.record(index).nameHash == coney::crc32(name));
    }
}
