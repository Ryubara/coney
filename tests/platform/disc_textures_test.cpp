// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every texture dictionary in WARRIORS.WAD read with librw and converted to
// RGBA images. It runs only when the environment variable CONEY_DISC names the disc (a mounted disc, a folder or an
// ISO image; read with SDL_getenv, which every compiler accepts without a deprecation warning), and skips otherwise, so
// CI never needs the game. It prints counts only, never data (LEGAL.md).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "core/chunk_types.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/load_entry_mode.h"
#include "graphics/rw_stream.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"

namespace {

// Totals over the whole disc.
struct Totals {
    std::uint64_t containerEntries = 0;             // entries that load as chunk containers
    std::uint64_t containersWithTxd = 0;            // ... of which hold at least one dictionary
    std::uint64_t headeredEntries = 0;              // headered RenderWare streams starting with a dictionary
    std::uint64_t worldStreams = 0;                 // world streams: a u32, then a dictionary
    std::uint64_t dictionaries = 0;                 // dictionaries read
    std::uint64_t textures = 0;                     // textures read
    std::uint64_t converted = 0;                    // textures converted to RGBA images
    std::uint64_t skipped = 0;                      // textures left out because librw would overrun its buffers
    std::uint64_t failedEntries = 0;                // entries whose dictionaries failed to read or convert
    std::map<std::uint32_t, std::uint64_t> formats; // format << 16 | layout version << 8 | depth -> textures
};

// Counts one entry's dictionaries, converting every texture to an image.
void countDictionaries(std::vector<coney::platform::TextureDictionary>& dictionaries, Totals& totals) {
    for (coney::platform::TextureDictionary& dictionary : dictionaries) {
        ++totals.dictionaries;
        totals.textures += dictionary.info().textures.size();
        totals.skipped += dictionary.skippedTextures();
        for (const coney::graphics::Ps2TextureInfo& texture : dictionary.info().textures) {
            ++totals.formats[texture.rasterFormat << 16 | static_cast<std::uint32_t>(texture.version) << 8 |
                             texture.depth];
        }
        auto images = dictionary.toImages();
        if (images) {
            totals.converted += images->size();
        } else {
            ++totals.failedEntries;
            std::printf("  conversion failed: %s\n", images.error().message.c_str());
        }
    }
}

} // namespace

TEST_CASE("every texture dictionary on the disc reads and converts", "[disc][texture_dictionary]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(table);

    Totals totals;
    for (const coney::io::WadEntry& entry : wad->index().entries()) {
        auto stream = wad->openEntry(entry);
        REQUIRE(stream.has_value());
        std::vector<std::byte> start(std::min<std::uint64_t>(stream->size(), 28));
        REQUIRE(stream->read(start).has_value());
        auto headered = coney::graphics::detectHeaderedRwStream(start);
        if (headered && headered->firstSection.id == coney::graphics::kRwTexDictionary) {
            ++totals.headeredEntries;
        } else if (coney::graphics::isWorldStream(start)) {
            ++totals.worldStreams;
        } else if (!coney::loadWadEntry(*wad, entry, coney::chunk::ChunkHandlerTable::withDefaults())) {
            continue; // not a container (Lua, text, sound, world streams): nothing of ours to read
        } else {
            ++totals.containerEntries;
        }
        auto dictionaries = coney::platform::loadTextureDictionaries(*wad, entry, table);
        if (!dictionaries) {
            if (dictionaries.error().code != coney::ErrorCode::NotFound) {
                ++totals.failedEntries;
                std::printf("  entry %u: %s\n", entry.index, dictionaries.error().message.c_str());
            }
            continue;
        }
        if (!headered && !coney::graphics::isWorldStream(start)) {
            ++totals.containersWithTxd;
        }
        countDictionaries(*dictionaries, totals);
    }

    std::printf("chunk containers: %llu, with dictionaries: %llu; sector atomics files: %llu; world streams: %llu\n",
                static_cast<unsigned long long>(totals.containerEntries),
                static_cast<unsigned long long>(totals.containersWithTxd),
                static_cast<unsigned long long>(totals.headeredEntries),
                static_cast<unsigned long long>(totals.worldStreams));
    std::printf("dictionaries: %llu, textures read: %llu, converted: %llu, left out: %llu, failed entries: %llu\n",
                static_cast<unsigned long long>(totals.dictionaries), static_cast<unsigned long long>(totals.textures),
                static_cast<unsigned long long>(totals.converted), static_cast<unsigned long long>(totals.skipped),
                static_cast<unsigned long long>(totals.failedEntries));
    for (const auto& [key, count] : totals.formats) {
        std::printf("  raster format %#06x, layout version %u, depth %u: %llu textures\n", key >> 16,
                    (key >> 8) & 0xFFU, key & 0xFFU, static_cast<unsigned long long>(count));
    }
    CHECK(totals.failedEntries == 0);
    CHECK(totals.converted == totals.textures);
}
