// SPDX-License-Identifier: GPL-3.0-or-later

// The gangs' spawners (ai/spawners.h): a spawner's state decides when it is ready, and its delay, its limit of humans
// alive at once and its total decide whether it makes one; each made human is named after it and goes to its callback.

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/route_planner.h"
#include "ai/spawners.h"
#include "support/path_fixtures.h"
#include "world/path_map.h"

namespace {

// A level with player 1 at `player`, where every spawned human is alive until killed.
class TestWorld final : public coney::ai::SpawnerWorld {
  public:
    [[nodiscard]] std::optional<coney::anim::Vec3> playerPosition() const override { return player; }
    [[nodiscard]] std::optional<coney::anim::Vec3> outOfSight(float value, int gang) override {
        asked.emplace_back(value, gang);
        return spot;
    }
    [[nodiscard]] bool alive(double handle) const override { return !dead.contains(handle); }
    [[nodiscard]] bool seen(coney::anim::Vec3 centre, float radius) const override {
        looked.emplace_back(centre, radius);
        return camera;
    }
    double spawn(const coney::ai::SpawnRequest& request) override {
        made.push_back(request);
        return static_cast<double>(100 + made.size());
    }
    void spawned(std::string_view callback, double handle, int gang, std::string_view spawner) override {
        callbacks.push_back(std::string(callback) + ":" + std::to_string(static_cast<int>(handle)) + ":" +
                            std::to_string(gang) + ":" + std::string(spawner));
    }

    std::optional<coney::anim::Vec3> player = coney::anim::Vec3{0.0F, 0.0F, 0.0F};
    std::optional<coney::anim::Vec3> spot = coney::anim::Vec3{1.0F, 2.0F, 3.0F};
    std::vector<std::pair<float, int>> asked;
    std::set<double> dead;
    bool camera = false;
    mutable std::vector<std::pair<coney::anim::Vec3, float>> looked;
    std::vector<coney::ai::SpawnRequest> made;
    std::vector<std::string> callbacks;
};

// A spawner on gang 3 at (10, 0, 0) making types 5 and 7 (then a 0 ending the list), at most two alive, 500 ms apart.
coney::script::SpawnerCall spawnerCall(int state) {
    coney::script::SpawnerCall call;
    call.gang = 3;
    call.name = "ENEMYspawner";
    call.types = {5, 7, 0, 0, 0, 0, 0, 0, 0, 0};
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

// A square level 400 m wide with a line of route nodes along y through the camera at the origin: the player's node
// 0 at (0, 5), ahead 1 (0, 20), 2 (0, 40), 3 (0, 60) and 4 (0, 80), behind 5 (0, -10) and 6 (0, -30). The link from
// 3 to 4 carries the avoid bit when `avoidAhead`; `behind` false leaves out the nodes behind.
coney::world::PathMap lineOfNodes(bool avoidAhead, bool behind = true) {
    coney::test::PathBuilder builder;
    const std::uint32_t square = builder.rectangle(-200.0F, 200.0F, -200.0F, 200.0F);
    for (const float y : {5.0F, 20.0F, 40.0F, 60.0F, 80.0F}) {
        builder.node(square, 0.0F, y);
    }
    if (behind) {
        builder.node(square, 0.0F, -10.0F);
        builder.node(square, 0.0F, -30.0F);
        builder.link(0, 5);
        builder.link(5, 6);
    }
    builder.link(0, 1);
    builder.link(1, 2);
    builder.link(2, 3);
    builder.link(3, 4, 1, avoidAhead);
    return builder.build();
}

// The camera at the origin looking along +y, 90 degrees wide.
const coney::ai::PlacementCamera kCamera{
    .eye = {0.0F, 0.0F, 0.0F}, .forward = {0.0F, 1.0F, 0.0F}, .halfFovRadians = std::numbers::pi_v<float> / 4.0F};

} // namespace

TEST_CASE("an on spawner makes a human per delay up to its limit alive, and another when one goes down",
          "[ai][spawners]") {
    coney::ai::Spawners spawners;
    spawners.add(spawnerCall(1));
    TestWorld world;
    spawners.update(0, world);
    REQUIRE(world.made.size() == 1);
    CHECK(world.made[0].name == "ENEMYspawner0");
    // The index moves on before the pick, so a new spawner starts with its second type.
    CHECK(world.made[0].type == 7);
    CHECK(world.made[0].gang == 3);
    CHECK(world.made[0].model == "warr_sw");
    CHECK(world.made[0].heading == 33);
    CHECK(world.callbacks == std::vector<std::string>{"CreateENEMY:101:3:ENEMYspawner"});

    // Not before the delay; then the next type of the list, back to the first at the 0.
    spawners.update(400, world);
    CHECK(world.made.size() == 1);
    spawners.update(500, world);
    REQUIRE(world.made.size() == 2);
    CHECK(world.made[1].type == 5);
    // Two alive: no third until one is down.
    spawners.update(2000, world);
    CHECK(world.made.size() == 2);
    world.dead.insert(101.0);
    spawners.update(2100, world);
    REQUIRE(world.made.size() == 3);
    CHECK(world.made[2].type == 7);
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
    CHECK(world.asked[0] == std::pair<float, int>{15.0F, 3});
    // None out of sight: no spawn this update, and the next update tries again.
    world.spot.reset();
    spawners.update(500, world);
    CHECK(world.made.size() == 1);
    world.spot = coney::anim::Vec3{4.0F, 5.0F, 6.0F};
    spawners.update(510, world);
    REQUIRE(world.made.size() == 2);
    CHECK(world.made[1].position == std::array<float, 3>{4.0F, 5.0F, 6.0F});
}

TEST_CASE("a spawner whose second type is 0 makes its first type every time", "[ai][spawners]") {
    coney::ai::Spawners spawners;
    coney::script::SpawnerCall call = spawnerCall(1);
    call.types = {5, 0, 7, 0, 0, 0, 0, 0, 0, 0};
    call.maxConcurrent = 3;
    spawners.add(call);
    TestWorld world;
    spawners.update(0, world);
    spawners.update(500, world);
    spawners.update(1000, world);
    REQUIRE(world.made.size() == 3);
    // The 0 ends the list, so the 7 after it is never reached.
    for (const coney::ai::SpawnRequest& request : world.made) {
        CHECK(request.type == 5);
    }
}

TEST_CASE("the out-of-sight search heads for the goal ahead and takes the first node beyond 70 m", "[ai][spawners]") {
    const coney::world::PathMap map = lineOfNodes(false);
    const coney::ai::RoutePlanner planner(map);
    int draws = 0;
    const coney::ai::PlacementRandom random = [&draws] {
        ++draws;
        return 0.5F;
    };
    // Toward the goal 100 m ahead the search walks the line ahead, all seen, to node 4 at 80 m.
    CHECK(coney::ai::outOfSightNode(planner, {0.0F, 5.0F, 0.0F}, kCamera, 15.0F, random) ==
          coney::anim::Vec3{0.0F, 80.0F, 0.0F});
    // One draw before the first search, unused.
    CHECK(draws == 1);
}

TEST_CASE("the out-of-sight search skips avoided links and takes a node beyond the value outside the cone",
          "[ai][spawners]") {
    const coney::world::PathMap map = lineOfNodes(true);
    const coney::ai::RoutePlanner planner(map);
    const coney::ai::PlacementRandom random = [] { return 0.5F; };
    // Node 4 is cut off, so the search falls back behind: node 5 is only 10 m away, node 6 30 m and behind.
    CHECK(coney::ai::outOfSightNode(planner, {0.0F, 5.0F, 0.0F}, kCamera, 15.0F, random) ==
          coney::anim::Vec3{0.0F, -30.0F, 0.0F});
    // A value of 40 leaves no node beyond it outside the cone.
    CHECK_FALSE(coney::ai::outOfSightNode(planner, {0.0F, 5.0F, 0.0F}, kCamera, 40.0F, random).has_value());
}

TEST_CASE("the out-of-sight search gives up after 17 tries, an angle drawn before each", "[ai][spawners]") {
    const coney::world::PathMap map = lineOfNodes(true, false);
    const coney::ai::RoutePlanner planner(map);
    int draws = 0;
    const coney::ai::PlacementRandom random = [&draws] {
        ++draws;
        return 0.25F;
    };
    CHECK_FALSE(coney::ai::outOfSightNode(planner, {0.0F, 5.0F, 0.0F}, kCamera, 15.0F, random).has_value());
    CHECK(draws == coney::ai::kPlacementTries);
}

TEST_CASE("a spawner with a negative limit spawns in waves, each once the last has all died", "[ai][spawners]") {
    coney::ai::Spawners spawners;
    spawners.add(spawnerCall(1));
    TestWorld world;
    spawners.setMaxConcurrent(3, "ENEMYspawner", -2);
    spawners.update(0, world);
    spawners.update(500, world);
    REQUIRE(world.made.size() == 2);
    // One of the wave dies: the wave is not over, so no new human.
    world.dead.insert(101.0);
    spawners.update(1000, world);
    CHECK(world.made.size() == 2);
    // Both dead: the next wave fills up.
    world.dead.insert(102.0);
    spawners.update(1500, world);
    spawners.update(2000, world);
    CHECK(world.made.size() == 4);
    spawners.update(2500, world);
    CHECK(world.made.size() == 4);
    // An unknown name changes nothing.
    spawners.setMaxConcurrent(3, "nobody", 9);
    CHECK(spawners.find(3, "ENEMYspawner")->maxConcurrent == -2);
}

TEST_CASE("an off-screen spawner waits while a camera sees the sphere above its spot", "[ai][spawners]") {
    coney::ai::Spawners spawners;
    spawners.add(spawnerCall(1));
    spawners.setMustBeOffScreen(3, "ENEMYspawner", true);
    TestWorld world;
    world.camera = true;
    spawners.update(0, world);
    CHECK(world.made.empty());
    REQUIRE(world.looked.size() == 1);
    CHECK(world.looked[0].first == coney::anim::Vec3{10.0F, 0.0F, 1.6F});
    CHECK(world.looked[0].second == 0.3F);
    world.camera = false;
    spawners.update(100, world);
    CHECK(world.made.size() == 1);
    // Off again: the camera is not asked.
    spawners.setMustBeOffScreen(3, "ENEMYspawner", false);
    world.camera = true;
    spawners.update(600, world);
    CHECK(world.made.size() == 2);
    CHECK(world.looked.size() == 2);
}

TEST_CASE("the out-of-sight search makes no try when the player has no start node", "[ai][spawners]") {
    const coney::world::PathMap map = lineOfNodes(false);
    const coney::ai::RoutePlanner planner(map);
    int draws = 0;
    const coney::ai::PlacementRandom random = [&draws] {
        ++draws;
        return 0.5F;
    };
    // Far off every polygon: no node to start from.
    CHECK_FALSE(coney::ai::outOfSightNode(planner, {500.0F, 500.0F, 0.0F}, kCamera, 15.0F, random).has_value());
    CHECK(draws == 0);
}
