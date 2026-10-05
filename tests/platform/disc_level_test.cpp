// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every level file (`<level>.lev`) loads through the chunk system with all of
// its handlers, on librw's NULL device, into a level object: collision, occluders, path data, the three linked models,
// the glow world and the subtitles. It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts only, never data (LEGAL.md). docs/research/level-loading.md has the results.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "platform/level_file.h"
#include "platform/render_engine.h"

namespace {

// Totals over every level file.
struct LevelTotals {
    std::uint64_t levels = 0;
    std::uint64_t failures = 0;
    std::uint64_t occluders = 0;
    std::uint64_t paths = 0;
    std::uint64_t pathListBytes = 0;        // the paths' slab edge lists
    std::uint64_t pathBytesUndescribed = 0; // chunk bytes past the records and lists: the tails
    std::uint64_t subtitleBytes = 0;
    std::uint64_t modelTriangles[3] = {0, 0, 0}; // skyline, sky box, cloud box
    std::uint64_t texturedModels = 0;            // models whose first material has a texture after linking
    std::uint64_t worldTriangles = 0;
    std::uint64_t worldTextured = 0;   // glow worlds whose materials all found their texture
    std::uint64_t boxesNearOrigin = 0; // sky and cloud boxes within 2 of the origin, as drawn round the camera
};

// Triangles of a level atomic's plain geometry.
std::uint64_t trianglesOf(const coney::chunk::LoadedObject* object) {
    rw::Atomic* atomic = coney::platform::levelAtomic(object);
    return atomic == nullptr ? 0 : static_cast<std::uint64_t>(atomic->geometry->numTriangles);
}

// Whether every material of a level atomic has a texture.
bool allTextured(const coney::chunk::LoadedObject* object) {
    rw::Atomic* atomic = coney::platform::levelAtomic(object);
    if (atomic == nullptr) {
        return false;
    }
    const rw::MaterialList& list = atomic->geometry->matList;
    for (rw::int32 i = 0; i < list.numMaterials; ++i) {
        if (list.materials[i]->texture == nullptr) {
            return false;
        }
    }
    return list.numMaterials > 0;
}

// Whether a model's world bounding sphere lies within `reach` of the origin.
bool nearOrigin(const coney::chunk::LoadedObject* object, float reach) {
    rw::Atomic* atomic = coney::platform::levelAtomic(object);
    if (atomic == nullptr) {
        return false;
    }
    const rw::Sphere* sphere = atomic->getWorldBoundingSphere();
    const rw::V3d c = sphere->center;
    return std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z) + sphere->radius <= reach;
}

} // namespace

TEST_CASE("every level file loads into a level object", "[disc][level]") {
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

    std::vector<std::string> levels{"objarena"};
    for (int level = 0; level < 200; ++level) {
        levels.push_back(std::format("level{}", level));
    }
    LevelTotals totals;
    for (const std::string& name : levels) {
        if (!wad->lookup(name + ".lev")) {
            continue;
        }
        ++totals.levels;
        auto level = coney::platform::loadLevel(*wad, name, false);
        if (!level) {
            ++totals.failures;
            std::printf("  %s\n", level.error().message.c_str());
            continue;
        }
        const coney::world::LevelObject& object = **level;
        totals.occluders += object.occluders.size();
        totals.paths += object.pathHeader.paths;
        totals.pathListBytes += object.pathHeader.edgeListBytes;
        totals.pathBytesUndescribed +=
            object.pathData.size() - object.pathHeader.recordBytes - object.pathHeader.edgeListBytes;
        totals.subtitleBytes += object.subtitles.size();
        const coney::world::LevelModel* models[3] = {&object.skyline, &object.skyBox, &object.cloudBox};
        for (int m = 0; m < 3; ++m) {
            totals.modelTriangles[m] += trianglesOf(models[m]->model.get());
            rw::Atomic* atomic = coney::platform::levelAtomic(models[m]->model.get());
            if (atomic != nullptr && atomic->geometry->matList.materials[0]->texture != nullptr) {
                ++totals.texturedModels;
            }
        }
        totals.boxesNearOrigin += (nearOrigin(object.skyBox.model.get(), 2.0F) ? 1U : 0U) +
                                  (nearOrigin(object.cloudBox.model.get(), 2.0F) ? 1U : 0U);
        totals.worldTriangles += trianglesOf(object.levelWorld.get());
        totals.worldTextured += allTextured(object.levelWorld.get()) ? 1U : 0U;
    }

    std::printf("level files: %llu (failed %llu); occluders %llu; paths %llu (edge lists %llu bytes, tails %llu "
                "bytes); subtitles %llu bytes\n",
                static_cast<unsigned long long>(totals.levels), static_cast<unsigned long long>(totals.failures),
                static_cast<unsigned long long>(totals.occluders), static_cast<unsigned long long>(totals.paths),
                static_cast<unsigned long long>(totals.pathListBytes),
                static_cast<unsigned long long>(totals.pathBytesUndescribed),
                static_cast<unsigned long long>(totals.subtitleBytes));
    std::printf(
        "model triangles: skyline %llu, sky box %llu, cloud box %llu; %llu of %llu models textured; %llu sky "
        "and cloud boxes within 2 of the origin; glow world triangles %llu, %llu worlds fully textured\n",
        static_cast<unsigned long long>(totals.modelTriangles[0]),
        static_cast<unsigned long long>(totals.modelTriangles[1]),
        static_cast<unsigned long long>(totals.modelTriangles[2]),
        static_cast<unsigned long long>(totals.texturedModels), static_cast<unsigned long long>(totals.levels) * 3,
        static_cast<unsigned long long>(totals.boxesNearOrigin), static_cast<unsigned long long>(totals.worldTriangles),
        static_cast<unsigned long long>(totals.worldTextured));
    CHECK(totals.levels == 64);
    CHECK(totals.failures == 0);
    CHECK(totals.occluders == 66);
    CHECK(totals.pathListBytes == 77678);
    CHECK(totals.pathBytesUndescribed == 770);
    CHECK(totals.texturedModels == totals.levels * 3);
    CHECK(totals.boxesNearOrigin == totals.levels * 2);
    CHECK(totals.worldTextured == totals.levels);
    CHECK(totals.worldTriangles == 6870);
}
