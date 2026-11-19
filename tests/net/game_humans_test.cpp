// SPDX-License-Identifier: GPL-3.0-or-later
// The server's game as snapshots see it (docs/guides/multiplayer.md#digest): each human's replicated state read from
// the game, the network ids the humans get and lose as they come and go, and the digest of the same scene stepped
// twice. Synthetic humans on a synthetic floor; no game data.
#include "net/game_humans.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "animation/anim_math.h"
#include "combat/power_class.h"
#include "human/human.h"
#include "human/humans.h"
#include "net/client_world.h"
#include "net/net_id.h"
#include "net/snapshot.h"
#include "net/state_digest.h"
#include "raycast/collision_mesh.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using coney::anim::Vec3;
using coney::human::Human;
using namespace coney::net;
namespace ai = coney::ai;

namespace {

// Humans on a 40 m floor, each with a brain, stepped together with their brains: the play level's humans in small.
struct Scene {
    coney::test::FightCharacter character{coney::test::fightClips()};
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, -20.0F, 20.0F, -20.0F, 20.0F));
    std::vector<std::unique_ptr<Human>> humans;
    coney::human::Humans step;
    ai::Brains brains;

    Scene() { step.setBrains(brains.hook()); }

    // A gang human with its feet at (x, y) facing `headingDegrees` and `health` points.
    Human& add(float x, float y, float headingDegrees, std::int16_t health = 1400) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        made.setFighterProfile(coney::human::FighterProfile{
            .player = false, .powerClass = coney::combat::kPlayerPowerClass, .health = health});
        made.spawn(mesh.get(), Vec3{x, y, 0.0F}, headingDegrees);
        step.add(made, false);
        brains.add(made, ai::BrainType::Gang, ai::FightSettings{}, static_cast<std::uint32_t>(humans.size()));
        return made;
    }

    // Takes `human` out of the scene, as a level removing a human does.
    void remove(Human& human) {
        brains.remove(human);
        step.remove(human);
    }

    void run(int steps) {
        for (int k = 0; k < steps; ++k) {
            step.update(mesh.get());
        }
    }
};

constexpr float kHalfPositionStep = 2048.0F / ((1U << 18) - 1) / 2.0F; // the position fields' quantization, halved

} // namespace

TEST_CASE("a human's replicated state is read from the game") {
    // Expected: snapshots.md#human-state is the field list; the values are the human's own (Coney's code)
    Scene scene;
    Human& human = scene.add(3.0F, -4.0F, 90.0F, 1200);
    scene.run(2);

    const HumanState state = replicatedState(human);
    CHECK(state.position == human.position());
    CHECK(state.heading == human.heading());
    CHECK(state.health == 1200);
    CHECK(state.healthMax == 1200);
    CHECK(state.grounded == !human.airborne());
    CHECK_FALSE(state.knockedOut);
    // The newest layer is the clip playing.
    const auto& tasks = human.animator().tasks();
    REQUIRE(tasks.layerCount() >= 1);
    CHECK(state.layers[0].clip == static_cast<std::int32_t>(tasks.top()->animId()));
    CHECK(state.layers[0].fade == 1.0F);
    // Layers the stack does not hold are "no such layer".
    for (std::size_t i = tasks.layerCount(); i < state.layers.size(); ++i) {
        CHECK(state.layers.at(i).clip == 0);
        CHECK(state.layers.at(i).fade == 1.0F);
    }
}

TEST_CASE("the game's humans get ids in brain order, keep them, and lose them when they go") {
    // Expected: multiplayer.md#net-ids (ids allocated in order, never reused in a generation)
    Scene scene;
    scene.add(0.0F, 0.0F, 0.0F);
    Human& second = scene.add(2.0F, 0.0F, 0.0F);
    GameHumans game;
    game.capture(scene.brains);
    REQUIRE(game.humans().size() == 2);
    CHECK(game.humans()[0].id == NetId{.slot = 0, .generation = 1});
    CHECK(game.humans()[1].id == NetId{.slot = 1, .generation = 1});
    CHECK(game.holding().size() == 2);
    CHECK(game.holding().at(NetId{.slot = 1, .generation = 1}) == humanWords(replicatedState(second)));

    scene.run(1);
    game.capture(scene.brains);
    REQUIRE(game.humans().size() == 2);
    CHECK(game.humans()[1].id == NetId{.slot = 1, .generation = 1});

    scene.remove(second);
    scene.add(4.0F, 0.0F, 0.0F);
    game.capture(scene.brains);
    REQUIRE(game.humans().size() == 2);
    CHECK(game.humans()[0].id == NetId{.slot = 0, .generation = 1});
    // The new human never takes the gone one's id: its slot is retired, not free.
    CHECK(game.humans()[1].id == NetId{.slot = 2, .generation = 1});
    CHECK_FALSE(game.holding().contains(NetId{.slot = 1, .generation = 1}));
}

TEST_CASE("a snapshot's words hold the game's positions within quantization") {
    // Expected: multiplayer.md#field-tables (18 bits over -1,024 to 1,024 m)
    Scene scene;
    Human& human = scene.add(12.345F, -6.789F, 30.0F);
    scene.run(3);
    GameHumans game;
    game.capture(scene.brains);
    REQUIRE(game.humans().size() == 1);
    const HumanState decoded = humanState(game.humans()[0].words);
    CHECK(std::abs(decoded.position.x - human.position().x) <= kHalfPositionStep);
    CHECK(std::abs(decoded.position.y - human.position().y) <= kHalfPositionStep);
}

TEST_CASE("the same scene stepped twice gives the same digests, and a change moves them") {
    // Expected: unverified, #558 (Coney's own digest of a deterministic step)
    const auto digests = [](float nudge) {
        Scene scene;
        scene.add(0.0F, 0.0F, 0.0F);
        scene.add(1.5F + nudge, 0.5F, 180.0F);
        GameHumans game;
        std::vector<std::uint64_t> each;
        for (int k = 0; k < 20; ++k) {
            scene.run(1);
            game.capture(scene.brains);
            each.push_back(humansDigest(game.holding()));
        }
        return each;
    };
    CHECK(digests(0.0F) == digests(0.0F));
    CHECK(digests(0.0F) != digests(0.5F));
}

TEST_CASE("clearing the humans, as a level going does, retires their ids") {
    // Expected: multiplayer.md#net-ids (a retired id is not handed out again in its generation)
    Scene scene;
    scene.add(0.0F, 0.0F, 0.0F);
    GameHumans game;
    game.capture(scene.brains);
    game.clear();
    CHECK(game.humans().empty());
    CHECK(game.holding().empty());
    game.capture(scene.brains);
    REQUIRE(game.humans().size() == 1);
    CHECK(game.humans()[0].id == NetId{.slot = 1, .generation = 1});
}

TEST_CASE("released ids keep a long run of humans coming and going from running out", "[net]") {
    // Expected: multiplayer.md#net-ids (65,536 slots; a released slot is used again with the next generation)
    Scene scene;
    scene.add(0.0F, 0.0F, 0.0F);
    GameHumans game;
    // More humans than there are slots, one at a time: each level's humans go and nobody can still name them.
    constexpr std::size_t kCycles = kNetIdMaxSlots + 10;
    for (std::size_t i = 0; i < kCycles; ++i) {
        game.capture(scene.brains);
        REQUIRE(game.humans().size() == 1);
        game.clear();
        game.releaseRetired();
    }
    // Every slot was used once; the ten after them are the oldest released slots, in their second generation.
    game.capture(scene.brains);
    REQUIRE(game.humans().size() == 1);
    CHECK(game.humans()[0].id == NetId{.slot = 10, .generation = 2});
}

TEST_CASE("with capture off the game's humans hold nothing and keep no ids", "[net]") {
    // Expected: unverified, #558 (Coney's own: only a session, --digest or --net-replay captures)
    Scene scene;
    scene.add(0.0F, 0.0F, 0.0F);
    scene.add(2.0F, 0.0F, 0.0F);
    GameHumans game;
    CHECK(game.capturing());
    game.capture(scene.brains);
    REQUIRE(game.humans().size() == 2);
    game.setCapturing(false);
    CHECK_FALSE(game.capturing());
    CHECK(game.humans().empty());
    game.capture(scene.brains);
    CHECK(game.humans().empty());
    CHECK(game.holding().empty());
    // Turned off and on more times than there are slots, it never runs out: turning off gives every id back.
    for (std::size_t i = 0; i < kNetIdMaxSlots / 2 + 10; ++i) {
        game.setCapturing(true);
        game.capture(scene.brains);
        REQUIRE(game.humans().size() == 2);
        game.setCapturing(false);
    }
    CHECK(game.humans().empty());
}
