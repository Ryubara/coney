// SPDX-License-Identifier: GPL-3.0-or-later

// The gangs' spawners (ai/spawners.h): a spawner's state decides when it is ready, and its delay, its limit of humans
// alive at once and its total decide whether it makes one; each made human is named after it and goes to its callback.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/spawners.h"

namespace {

// A level with player 1 at `player`, where every spawned human is alive until killed.
class TestWorld final : public coney::ai::SpawnerWorld {
  public:
    [[nodiscard]] std::optional<coney::anim::Vec3> playerPosition() const override { return player; }
    [[nodiscard]] std::optional<coney::anim::Vec3> outOfSight(float metres, std::size_t turn) const override {
        asked.emplace_back(metres, turn);
        return spot;
    }
    [[nodiscard]] bool alive(double handle) const override { return !dead.contains(handle); }
    double spawn(const coney::ai::SpawnRequest& request) override {
        made.push_back(request);
        return static_cast<double>(100 + made.size());
    }
    void spawned(std::string_view callback, double handle) override {
        callbacks.push_back(std::string(callback) + ":" + std::to_string(static_cast<int>(handle)));
    }

    std::optional<coney::anim::Vec3> player = coney::anim::Vec3{0.0F, 0.0F, 0.0F};
    std::optional<coney::anim::Vec3> spot;
    mutable std::vector<std::pair<float, std::size_t>> asked;
    std::set<double> dead;
    std::vector<coney::ai::SpawnRequest> made;
    std::vector<std::string> callbacks;
};

// A spawner on gang 3 at (10, 0, 0) making types 5 and 7, at most two alive, 500 ms apart.
coney::script::SpawnerCall spawnerCall(int state) {
    coney::script::SpawnerCall call;
    call.gang = 3;
    call.name = "ENEMYspawner";
    call.types = {5, 0, 7, 0, 0, 0, 0, 0, 0, 0};
    call.model = "warr_sw";
    call.position = {10.0F, 0.0F, 0.0F};
    call.heading = 33;
    call.total = -1;
    call.delayMs = 500;
    call.maxConcurrent = 2;
    call.state = state;
    call.callback = "CreateENEMY";
    call.value = 15;
    return call;
}

} // namespace

TEST_CASE("an on spawner makes a human per delay up to its limit alive, and another when one goes down",
          "[ai][spawners]") {
    coney::ai::Spawners spawners;
    spawners.add(spawnerCall(1));
    TestWorld world;
    spawners.update(0, world);
    REQUIRE(world.made.size() == 1);
    CHECK(world.made[0].name == "ENEMYspawner0");
    CHECK(world.made[0].type == 5);
    CHECK(world.made[0].gang == 3);
    CHECK(world.made[0].model == "warr_sw");
    CHECK(world.made[0].heading == 33);
    CHECK(world.callbacks == std::vector<std::string>{"CreateENEMY:101"});

    // Not before the delay; then the next type of the list.
    spawners.update(400, world);
    CHECK(world.made.size() == 1);
    spawners.update(500, world);
    REQUIRE(world.made.size() == 2);
    CHECK(world.made[1].type == 7);
    // Two alive: no third until one is down.
    spawners.update(2000, world);
    CHECK(world.made.size() == 2);
    world.dead.insert(101.0);
    spawners.update(2100, world);
    REQUIRE(world.made.size() == 3);
    CHECK(world.made[2].type == 5);
    CHECK(world.made[2].name == "ENEMYspawner2");
}

TEST_CASE("a spawner stops once it has made its total", "[ai][spawners]") {
    coney::ai::Spawners spawners;
    coney::script::SpawnerCall call = spawnerCall(8);
    call.total = 2;
    call.maxConcurrent = 10;
    spawners.add(call);
    TestWorld world;
    for (std::uint64_t now = 0; now <= 5000; now += 100) {
        spawners.update(now, world);
    }
    CHECK(world.made.size() == 2);
    const coney::ai::Spawner* spawner = spawners.find(3, "ENEMYspawner");
    REQUIRE(spawner != nullptr);
    CHECK_FALSE(spawner->inUse);
}

TEST_CASE("a stopped spawner waits for GangStartSpawner, and the states by time and distance", "[ai][spawners]") {
    coney::ai::Spawners spawners;
    spawners.add(spawnerCall(0));
    TestWorld world;
    spawners.update(0, world);
    CHECK(world.made.empty());

    // Unknown names, gangs and modes the original refuses change nothing; the value is still written.
    spawners.start(3, "nobody", 1, -1);
    spawners.start(4, "ENEMYspawner", 1, -1);
    spawners.start(3, "ENEMYspawner", 6, 20);
    spawners.update(100, world);
    CHECK(world.made.empty());
    CHECK(spawners.find(3, "ENEMYspawner")->value == 20);

    // Delayed: 2 s after the state was set.
    spawners.start(3, "ENEMYspawner", 2, 2);
    spawners.update(1000, world);
    CHECK(world.made.empty());
    spawners.update(2100, world);
    CHECK(world.made.size() == 1);

    // Near the player: player 1 within 5 m of the spawner, 10 m off at first.
    spawners.start(3, "ENEMYspawner", 3, 5);
    spawners.update(3000, world);
    CHECK(world.made.size() == 1);
    world.player = coney::anim::Vec3{6.0F, 0.0F, 0.0F};
    spawners.update(3100, world);
    CHECK(world.made.size() == 2);

    // Far from the player: beyond 5 m.
    world.dead = {101.0, 102.0};
    spawners.start(3, "ENEMYspawner", 5, 5);
    spawners.update(4000, world);
    CHECK(world.made.size() == 2);
    world.player = coney::anim::Vec3{-6.0F, 0.0F, 0.0F};
    spawners.update(4100, world);
    CHECK(world.made.size() == 3);
}

TEST_CASE("a gang keeps at most four spawners", "[ai][spawners]") {
    coney::ai::Spawners spawners;
    for (int i = 0; i < 5; ++i) {
        coney::script::SpawnerCall call = spawnerCall(0);
        call.name = "s" + std::to_string(i);
        spawners.add(call);
    }
    CHECK(spawners.of(3).size() == coney::ai::kGangSpawners);
    CHECK(spawners.find(3, "s4") == nullptr);
    CHECK(spawners.of(9).empty());
}

TEST_CASE("an out-of-sight spawner places its humans where the world finds a spot out of sight", "[ai][spawners]") {
    coney::ai::Spawners spawners;
    spawners.add(spawnerCall(8));
    TestWorld world;
    world.spot = coney::anim::Vec3{1.0F, 2.0F, 3.0F};
    spawners.update(0, world);
    REQUIRE(world.made.size() == 1);
    CHECK(world.made[0].position == std::array<float, 3>{1.0F, 2.0F, 3.0F});
    REQUIRE(world.asked.size() == 1);
    CHECK(world.asked[0] == std::pair<float, std::size_t>{15.0F, 0});
    // None out of sight: at the spawner.
    world.spot.reset();
    spawners.update(500, world);
    REQUIRE(world.made.size() == 2);
    CHECK(world.made[1].position == std::array<float, 3>{10.0F, 0.0F, 0.0F});
}

TEST_CASE("the out-of-sight spot is an unseen one nearest the distance, the best few taken in turn", "[ai][spawners]") {
    // The camera at the origin looking along +y, 90 degrees wide, 100 m deep; the player 5 m ahead of it.
    const coney::ai::SightCone view{
        .eye = {0.0F, 0.0F, 0.0F}, .forward = {0.0F, 1.0F, 0.0F}, .halfAngleRadians = 0.785398F, .range = 100.0F};
    const coney::anim::Vec3 player{0.0F, 5.0F, 0.0F};
    const std::vector<coney::anim::Vec3> spots{
        {0.0F, 20.0F, 0.0F},   // seen, 15 m from the player
        {0.0F, -10.0F, 0.0F},  // behind the camera, 15 m
        {-16.0F, 5.0F, 0.0F},  // to the side, 16 m
        {0.0F, -40.0F, 0.0F},  // behind, 45 m
        {30.0F, 5.0F, 0.0F},   // to the side, 30 m
        {0.0F, -300.0F, 0.0F}, // seen? no: behind, 305 m
    };
    CHECK(coney::ai::outOfSightSpot(spots, player, view, 15.0F, 0) == coney::anim::Vec3{0.0F, -10.0F, 0.0F});
    CHECK(coney::ai::outOfSightSpot(spots, player, view, 15.0F, 1) == coney::anim::Vec3{-16.0F, 5.0F, 0.0F});
    CHECK(coney::ai::outOfSightSpot(spots, player, view, 15.0F, 2) == coney::anim::Vec3{30.0F, 5.0F, 0.0F});
    CHECK(coney::ai::outOfSightSpot(spots, player, view, 15.0F, 4) == coney::anim::Vec3{0.0F, -10.0F, 0.0F});
    // Beyond the far clip is not seen either.
    const std::vector<coney::anim::Vec3> far{{0.0F, 200.0F, 0.0F}};
    CHECK(coney::ai::outOfSightSpot(far, player, view, 15.0F, 0) == coney::anim::Vec3{0.0F, 200.0F, 0.0F});
    const std::vector<coney::anim::Vec3> seenOnly{{0.0F, 20.0F, 0.0F}};
    CHECK_FALSE(coney::ai::outOfSightSpot(seenOnly, player, view, 15.0F, 0).has_value());
}
