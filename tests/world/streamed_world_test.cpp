// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/streamed_world.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/streaming_fixtures.h"

using Catch::Approx;
using coney::ErrorCode;
using coney::test::rowManifest;
using coney::test::rowOfSectors;
using coney::world::Box;
using coney::world::StreamedWorld;
using coney::world::Vec3;

TEST_CASE("the camera metric measures to the box's nearest face planes, inside or out", "[streamed_world]") {
    const Box box{{0.0F, 0.0F, 0.0F}, {10.0F, 10.0F, 10.0F}};
    const std::array<Vec3, 1> inside{Vec3{5.0F, 5.0F, 5.0F}};
    CHECK(coney::world::cameraDistanceSq(box, inside) == 75.0F); // 25 per axis: a camera inside is not at 0
    const std::array<Vec3, 2> two{Vec3{20.0F, 5.0F, 5.0F}, Vec3{5.0F, 5.0F, 5.0F}};
    CHECK(coney::world::cameraDistanceSq(box, two) == 75.0F); // the nearest camera counts
    CHECK(std::isinf(coney::world::cameraDistanceSq(box, {})));
    CHECK(coney::world::gameToRenderWareAxes(Vec3{1.0F, 2.0F, 3.0F}).z == -2.0F);
}

TEST_CASE("a world indexes its sectors and refuses inconsistent layouts", "[streamed_world]") {
    // Sectors 0..3 along x, parts 1, 1, 2, 3; a sector without an atomic in between.
    auto world = StreamedWorld::create("test", rowOfSectors({{0, 1}, {1, 1}, {-1, 0}, {2, 2}, {3, 3}}, 3),
                                       rowManifest({100, 200, 300}));
    REQUIRE(world.has_value());
    CHECK(world->sectors().size() == 4);
    CHECK(world->partCount() == 3);
    CHECK(world->part(1).sectors == std::vector<std::uint32_t>{0, 1});
    CHECK(world->part(3).sizes.heapSize == 300);

    CHECK(StreamedWorld::create("count", rowOfSectors({{0, 1}}, 2), rowManifest({1})).error().code ==
          ErrorCode::Invalid);
    CHECK(StreamedWorld::create("gap", rowOfSectors({{0, 1}, {2, 1}}, 1), rowManifest({1})).error().code ==
          ErrorCode::Invalid);
    CHECK(StreamedWorld::create("twice", rowOfSectors({{0, 1}, {0, 1}}, 1), rowManifest({1})).error().code ==
          ErrorCode::Invalid);
    CHECK(StreamedWorld::create("part", rowOfSectors({{0, 2}}, 1), rowManifest({1})).error().code ==
          ErrorCode::Invalid);
}

TEST_CASE("the search prefers visible sectors, within reach by the original's squared comparison", "[streamed_world]") {
    // Sector k spans x 10k..10k+10; the camera sits inside sector 0.
    auto world =
        StreamedWorld::create("test", rowOfSectors({{0, 1}, {1, 2}, {2, 3}, {3, 4}}, 4), rowManifest({1, 1, 1, 1}));
    REQUIRE(world.has_value());
    const std::array<Vec3, 1> camera{Vec3{5.0F, 5.0F, 5.0F}};
    // Only sector 3 is visible: its squared distance is 25² + 25 + 25 = 675.
    world->markVisible(3);

    // A draw distance of 1000 reaches 675: the visible sector wins over the nearer, unseen sector 0.
    CHECK(world->findSectorToLoad(camera, 1000.0F) == 3U);
    CHECK_FALSE(world->searchFellBack());
    CHECK(world->pendingDistance(camera) == Approx(std::sqrt(675.0F)));

    // At 500 the visible sector is "out of reach" although it is only 26 away: the squared distance is compared with
    // the plain draw distance (the original's quirk), so the search falls back to every sector.
    CHECK(world->findSectorToLoad(camera, 500.0F) == 0U);
    CHECK(world->searchFellBack());

    // With every part loaded nothing is missing.
    for (std::uint32_t part = 1; part <= 4; ++part) {
        world->markPartLoaded(part, 0);
    }
    CHECK_FALSE(world->findSectorToLoad(camera, 500.0F).has_value());
    CHECK(std::isinf(world->pendingDistance(camera)));
}

TEST_CASE("a failed part is not searched for again", "[streamed_world]") {
    auto world = StreamedWorld::create("test", rowOfSectors({{0, 1}, {1, 2}}, 2), rowManifest({1, 1}));
    REQUIRE(world.has_value());
    const std::array<Vec3, 1> camera{Vec3{5.0F, 5.0F, 5.0F}};
    world->markPartFailed(1);
    CHECK(world->part(1).state == coney::world::PartState::Failed);
    CHECK(world->findSectorToLoad(camera, 100.0F) == 1U);
}

TEST_CASE("the part to unload is the farthest loaded one nobody sees, never the last", "[streamed_world]") {
    auto world =
        StreamedWorld::create("test", rowOfSectors({{0, 1}, {1, 2}, {2, 3}, {3, 4}}, 4), rowManifest({1, 1, 1, 1}));
    REQUIRE(world.has_value());
    const std::array<Vec3, 1> camera{Vec3{5.0F, 5.0F, 5.0F}};
    CHECK_FALSE(world->findPartToUnload(camera).has_value()); // nothing loaded
    for (std::uint32_t part = 1; part <= 4; ++part) {
        world->markPartLoaded(part, 0);
    }
    // Part 4 is the farthest but the last part of the world: the original's loop stops before it.
    const auto found = world->findPartToUnload(camera);
    REQUIRE(found.has_value());
    const coney::world::UnloadCandidate candidate = found.value_or(coney::world::UnloadCandidate{});
    CHECK(candidate.part == 3);
    CHECK(candidate.distanceSq == 15.0F * 15.0F + 50.0F); // x 20..30 seen from 5: the nearer plane, 15 away
}

TEST_CASE("a part with a visible sector is not unloaded", "[streamed_world]") {
    auto world = StreamedWorld::create("test", rowOfSectors({{0, 1}, {1, 2}, {2, 3}}, 3), rowManifest({1, 1, 1}));
    REQUIRE(world.has_value());
    const std::array<Vec3, 1> camera{Vec3{5.0F, 5.0F, 5.0F}};
    for (std::uint32_t part = 1; part <= 3; ++part) {
        world->markPartLoaded(part, 0);
    }
    world->markVisible(1);
    const auto found = world->findPartToUnload(camera);
    REQUIRE(found.has_value());
    const coney::world::UnloadCandidate candidate = found.value_or(coney::world::UnloadCandidate{});
    CHECK(candidate.part == 1); // part 2 is seen and part 3 is the last
}

TEST_CASE("only visible, loaded sectors are collected, nearest first", "[streamed_world]") {
    auto world =
        StreamedWorld::create("test", rowOfSectors({{0, 1}, {1, 1}, {2, 2}, {3, 3}}, 3), rowManifest({1, 1, 1}));
    REQUIRE(world.has_value());
    world->markPartLoaded(1, 0);
    world->markPartLoaded(3, 0);
    // A frustum looking along -x from x = 100 sees the whole row.
    coney::world::CameraPose pose;
    pose.position = Vec3{100.0F, 5.0F, 5.0F};
    pose.forward = Vec3{-1.0F, 0.0F, 0.0F};
    pose.right = Vec3{0.0F, 0.0F, -1.0F};
    world->findVisibleSectors(coney::world::ViewFrustum(pose, 1.0F, 1.0F, 0.5F, 500.0F), true);
    const std::array<Vec3, 1> camera{pose.position};
    CHECK(world->collectSectors(camera) == std::vector<std::uint32_t>{3, 1, 0}); // sector 2's part is not loaded
}

TEST_CASE("a new atomic fades in over one second", "[streamed_world]") {
    CHECK(coney::world::fadeInAlpha(1000, 0) == 0);
    CHECK(coney::world::fadeInAlpha(1000, 500) == 127);
    CHECK(coney::world::fadeInAlpha(1000, 1000) == 255);
    CHECK(coney::world::fadeInAlpha(1000, 5000) == 255);
    auto world = StreamedWorld::create("test", rowOfSectors({{0, 1}}, 1), rowManifest({1}));
    REQUIRE(world.has_value());
    world->markPartLoaded(1, 250);
    CHECK(world->sectors()[0].fadeEndMs == 1250);
    CHECK(world->sectors()[0].loaded);
    world->markPartUnloaded(1);
    CHECK_FALSE(world->sectors()[0].loaded);
}
