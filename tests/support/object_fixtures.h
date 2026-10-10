// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic worlds for the glass panes', doors' and barriers' tests: a recording ObjectServices, a small path map with
// a door's links and a choke link, and a collision mesh of four upright triangles. No game data.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_random.h"
#include "raycast/collision_builder.h"
#include "raycast/collision_mesh.h"
#include "support/path_fixtures.h"
#include "world/path_map.h"
#include "world_objects/object_services.h"

namespace coney::test {

/// Keeps every call the panes and doors make of their services.
class RecordingServices final : public world_objects::ObjectServices {
  public:
    struct Crime {
        int type;
        anim::Vec3 at;
        double offender;
    };
    struct Script {
        std::string function;
        double human;
        double door;
    };

    std::vector<std::uint32_t> sounds;
    std::vector<int> cues; // playCueAt()
    std::vector<std::pair<std::uint8_t, std::uint8_t>> pairs;
    std::vector<float> volumes; // each pair's volume
    int shards = 0;
    float shardSize = 0.0F;
    std::vector<Crime> crimes;
    int crimeSceneMoves = 0;
    std::vector<std::pair<float, int>> flagsDisabled;
    std::vector<double> panesCounted;
    int stereosFreed = 0;
    int glassObjectBreaks = 0;
    std::vector<std::pair<double, bool>> bodies;
    std::vector<std::string> spawned;
    std::vector<anim::Vec3> spawnedAt; // where each spawned object was made
    std::vector<double> destroyed;
    std::vector<double> nextModels;
    std::vector<std::pair<double, std::uint32_t>> models;
    std::vector<std::pair<double, std::uint32_t>> values; // setValue()
    std::vector<std::pair<double, anim::Vec3>> moves;     // moveObject()
    int dusts = 0;
    int splinterCount = 0;
    int bursts = 0;
    std::vector<double> stopped;
    std::vector<Script> scripts;
    std::vector<std::pair<int, int>> scores;
    int clicks = 0;
    std::vector<std::pair<double, double>> damage; // (human, object)
    bool wantShards = true;

    void playSound(std::uint32_t nameHash, anim::Vec3 /*at*/) override { sounds.push_back(nameHash); }
    void playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 /*at*/, float volume = 1.0F) override {
        pairs.emplace_back(a, b);
        volumes.push_back(volume);
    }
    void playCueAt(int cue, anim::Vec3 /*at*/) override { cues.push_back(cue); }
    bool shardsWanted(anim::Vec3 /*centre*/) override { return wantShards; }
    void spawnShard(anim::Vec3 /*at*/, float size, std::uint32_t /*colour*/) override {
        ++shards;
        shardSize = size;
    }
    void reportCrime(int type, anim::Vec3 at, double offender) override { crimes.push_back({type, at, offender}); }
    void moveCrimeSceneFlag(anim::Vec3 /*at*/) override { ++crimeSceneMoves; }
    void disableFlagsNear(float radius, anim::Vec3 /*at*/, int activity) override {
        flagsDisabled.emplace_back(radius, activity);
    }
    void countPaneBroken(double breaker) override { panesCounted.push_back(breaker); }
    void freeCarStereos(anim::Vec3 /*at*/, float /*radius*/) override { ++stereosFreed; }
    void breakGlassObjects(anim::Vec3 /*centre*/, float /*radius*/) override { ++glassObjectBreaks; }
    void setBody(double object, bool present) override { bodies.emplace_back(object, present); }
    double spawnObject(std::string_view type, anim::Vec3 at, anim::Quat /*rotation*/) override {
        spawned.emplace_back(type);
        spawnedAt.push_back(at);
        return 1000.0 + static_cast<double>(spawned.size());
    }
    void destroyObject(double object) override { destroyed.push_back(object); }
    void nextModel(double object) override { nextModels.push_back(object); }
    void setModel(double object, std::uint32_t modelHash) override { models.emplace_back(object, modelHash); }
    void setValue(double object, std::uint32_t value) override { values.emplace_back(object, value); }
    void moveObject(double object, anim::Vec3 at) override { moves.emplace_back(object, at); }
    void dust(anim::Vec3 /*at*/, float /*radius*/) override { ++dusts; }
    void splinters(anim::Vec3 /*at*/, int count) override { splinterCount += count; }
    void burst(anim::Vec3 /*at*/) override { ++bursts; }
    void stopHumansTargeting(double object) override { stopped.push_back(object); }
    void callScript(std::string_view function, double human, double door) override {
        scripts.push_back({std::string(function), human, door});
    }
    void scoreEvent(double /*human*/, int category, int event) override { scores.emplace_back(category, event); }
    void lockPickClick(double /*human*/) override { ++clicks; }
    void damageDone(double human, double object) override { damage.emplace_back(human, object); }
};

/// The synthetic level's door number.
inline constexpr std::uint16_t kTestDoorNumber = 7;

/// Two rooms side by side, polygons 0 ([0, 4] × [0, 4], node 0 at (2, 2)) and 1 ([4, 8] × [0, 4], node 1 at (6, 2)),
/// linked both ways by a kind-0x10 link of door kTestDoorNumber (links 0 and 1); and a corridor, polygon 2
/// ([0, 8] × [10, 14], nodes 2 at (2, 12) and 3 at (6, 12)), linked both ways by a kind-4 choke link (links 2 and 3);
/// and the doorway's hole, polygon 3 ([3.6, 4.4] × [1, 3], off the graph, flags 7 as the disc's holes have).
inline world::PathMap objectPaths() {
    PathBuilder builder;
    const std::uint32_t left = builder.rectangle(0.0F, 4.0F, 0.0F, 4.0F);
    const std::uint32_t right = builder.rectangle(4.0F, 8.0F, 0.0F, 4.0F);
    const std::uint32_t corridor = builder.rectangle(0.0F, 8.0F, 10.0F, 14.0F);
    builder.rectangle(3.6F, 4.4F, 1.0F, 3.0F, 0, 0x7);
    builder.node(left, 2.0F, 2.0F);
    builder.node(right, 6.0F, 2.0F);
    builder.node(corridor, 2.0F, 12.0F);
    builder.node(corridor, 6.0F, 12.0F);
    builder.link(0, 1, 0x10, false, kTestDoorNumber);
    builder.link(2, 3, 0x4);
    return builder.build();
}

/// Four upright triangles: 0 and 1 a 2 × 2 quad in the plane x = 4 at y 1-3 (the door), 2 and 3 one at y 11-13 (the
/// pane), all concrete.
inline std::unique_ptr<raycast::CollisionMesh> objectMesh() {
    const auto quad = [](float y0) {
        const raycast::Vec3 a{4.0F, y0, 0.0F};
        const raycast::Vec3 b{4.0F, y0 + 2.0F, 0.0F};
        const raycast::Vec3 c{4.0F, y0 + 2.0F, 2.0F};
        const raycast::Vec3 d{4.0F, y0, 2.0F};
        return std::pair{raycast::BuildTriangle{.corners = {a, b, c}}, raycast::BuildTriangle{.corners = {a, c, d}}};
    };
    const auto [door0, door1] = quad(1.0F);
    const auto [pane0, pane1] = quad(11.0F);
    const std::vector<raycast::BuildTriangle> triangles{door0, door1, pane0, pane1};
    auto mesh = raycast::buildCollisionMesh(triangles, 2.0F);
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

/// A world over the fixtures above, with recording services and the stand-in random numbers.
struct ObjectWorldFixture {
    world::PathMap paths = objectPaths();
    std::unique_ptr<raycast::CollisionMesh> mesh = objectMesh();
    RecordingServices services;
    GameRandom random;
    world_objects::ObjectWorld world{mesh.get(), &paths, &services, &random, nullptr};
    double nextHandle = 100.0;

    /// Gives out handles from 100 up.
    double handle() { return nextHandle++; }
    /// Whether triangle `index` is enabled.
    [[nodiscard]] bool enabled(std::uint32_t index) const {
        return (mesh->triangles()[index].flags & raycast::kTriangleEnabled) != 0;
    }
};

} // namespace coney::test
