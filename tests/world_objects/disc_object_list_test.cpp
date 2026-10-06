// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the Object List of warriors.glr parses, every record names a model and a
// texture dictionary that are WAD entries, and every model goes through the level file's 0x47 reader (librw on its
// NULL device): a clump of one atomic as a model, a car's clump of 47 as several. The results are in
// docs/research/level-loading.md#the-object-list and docs/research/cars.md#model. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise, so CI never needs the game. It prints counts only, never data
// (LEGAL.md).

#include <cstdint>
#include <cstdio>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "characters/character_list.h"
#include "core/chunk_system.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/load_entry_mode.h"
#include "platform/level_file.h"
#include "platform/render_engine.h"
#include "world/level_object.h"
#include "world_objects/car_types.h"
#include "world_objects/object_list.h"

TEST_CASE("every Object List record names a model and textures, and every model loads, the cars' as 47 atomics",
          "[disc][object_list]") {
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

    auto list = coney::world_objects::loadObjectList(*wad);
    REQUIRE(list.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    table.setHandlers(coney::world::kPreinstanceObjectChunk,
                      coney::chunk::ChunkHandlers{{}, coney::platform::readPreinstanceObjectChunk});

    std::uint64_t texturesFound = 0;
    std::uint64_t modelsFound = 0;
    std::uint64_t loaded = 0;
    std::uint64_t clumps = 0;
    std::uint64_t refused = 0;
    for (const coney::world_objects::ObjectRecord& record : list->records()) {
        texturesFound += wad->lookup(coney::characters::resourceFileName(record.texturesHash)).has_value() ? 1 : 0;
        auto entry = wad->lookup(coney::characters::resourceFileName(record.modelHash));
        if (!entry) {
            continue;
        }
        ++modelsFound;
        auto load = coney::loadWadEntry(*wad, **entry, table);
        const auto models =
            load ? load->stacks.takeChunks(coney::world::kLevelModelResult) : std::vector<coney::chunk::ChunkData>{};
        if (models.empty()) {
            ++refused;
            continue;
        }
        ++loaded;
        // A car's model: one atomic per part and per damaged part.
        if (const auto* clump = dynamic_cast<const coney::platform::LevelClumpObject*>(models.front().object.get());
            clump != nullptr && clump->parts().size() == 47) {
            ++clumps;
        }
    }
    std::printf("Object List: %zu records, %llu models and %llu dictionaries found, %llu models loaded (%llu clumps of "
                "47 atomics), %llu refused\n",
                list->records().size(), static_cast<unsigned long long>(modelsFound),
                static_cast<unsigned long long>(texturesFound), static_cast<unsigned long long>(loaded),
                static_cast<unsigned long long>(clumps), static_cast<unsigned long long>(refused));
    CHECK(list->records().size() == 1406);
    CHECK(modelsFound == list->records().size());
    CHECK(texturesFound == list->records().size());
    CHECK(loaded == 1406);
    CHECK(clumps == coney::world_objects::kCarTypeNames.size());
    CHECK(refused == 0);
    CHECK(list->find("dyn_bat") != nullptr);
    for (const std::string_view car : coney::world_objects::kCarTypeNames) {
        CHECK(list->find(car) != nullptr);
    }
}
