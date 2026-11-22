// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the client game's scene of `level99` (docs/guides/client-world.md#client-game)
// loads the level and the stand-in character, and draws the humans of a client's world, fed snapshots as a guest is,
// each posed from its layers with the character's clips, the camera on the host's player 1. Headless, on librw's NULL
// device. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts
// only (LEGAL.md).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "hud/hud.h"
#include "hud/hud_owner.h"
#include "net/client_world.h"
#include "net/net_id.h"
#include "net/snapshot.h"
#include "platform/client_level_scene.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "world/sector_budget.h"

namespace {

// The disc named by CONEY_DISC, opened; nothing when it is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<coney::io::Wad>(std::move(*wad)) : std::nullopt;
}

// A human at `x` along from level99's start (-284.4, 120.4, 0.3; human::researchedStart()) playing `clip` alone, as a
// gait blend at `gait` when above 0.
coney::net::HumanState humanAt(float x, std::int32_t clip, float gait) {
    coney::net::HumanState human;
    human.position = coney::anim::Vec3{-284.4F + x, 120.4F, 0.3F};
    human.health = 150;
    human.healthMax = 150;
    human.grounded = true;
    human.layers[0] = coney::net::HumanAnimLayer{.clip = clip, .playhead = 0.25F, .gait = gait, .fade = 1.0F};
    return human;
}

} // namespace

// Expected: unverified, #1603 (Coney's own: the stand-in model and camera of docs/guides/client-world.md#client-game;
// the clips are AnimSlots::player()'s walk blend, 408 to 411, and the idle 388, human_animator.h)
TEST_CASE("the client game's scene of level99 draws a guest's humans posed with their clips, the camera on player 1",
          "[disc][net][client_game]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    // The HUD the client game would step, with the first player's panel up.
    coney::hud::HudOwner hudOwner;
    hudOwner.hud().attachPlayer(0, 0);
    hudOwner.hud().showAll();
    auto scene =
        coney::platform::ClientLevelScene::load(**engine, *wad, {.level = "level99", .checkpoint = 1}, budget, hudOwner,
                                                [&log](std::string_view line) { log.emplace_back(line); });
    REQUIRE(scene.has_value());
    coney::platform::ClientLevelScene& drawn = **scene;

    // Two humans, as a host's snapshots send them: player 1 walking in the walk-to-jog blend, another standing.
    coney::net::SnapshotSender sender;
    coney::net::SnapshotReceiver receiver;
    coney::net::ClientWorld world;
    const auto send = [&](std::uint32_t step, bool both) {
        std::vector<coney::net::SnapshotHuman> humans{
            {.id = {.slot = 0, .generation = 1},
             .words = coney::net::humanWords(humanAt(0.05F * static_cast<float>(step), 409, 0.6F)),
             .priority = 1}};
        if (both) {
            humans.push_back({.id = {.slot = 1, .generation = 1},
                              .words = coney::net::humanWords(humanAt(3.0F, 388, 0.0F)),
                              .priority = 1});
        }
        const coney::net::Snapshot snapshot = sender.build(step, humans);
        REQUIRE(receiver.apply(snapshot) == coney::net::SnapshotApplied::Applied);
        REQUIRE(sender.acknowledge(snapshot.sequence).has_value());
    };
    // One client step: the newest snapshot in, the world and the scene stepped, a frame drawn.
    std::uint64_t index = 0;
    const auto clientStep = [&] {
        world.receive(receiver);
        world.step();
        hudOwner.hud().update(coney::hud::HudFrame{.nowMs = 100 * (index + 1)});
        drawn.step(world, coney::FrameTime{.index = index++, .seconds = 1.0 / 30.0});
        drawn.render(world, coney::RenderTime{});
    };

    for (std::uint32_t step = 0; step < 10; ++step) {
        send(step, true);
        clientStep();
    }
    CHECK(drawn.humansPosed() == 2);
    CHECK(drawn.layersMissing() == 0);
    CHECK(drawn.meshes() == 2);
    // The HUD's newest step was queued for drawing over the frame.
    CHECK(drawn.hudSpritesQueued() > 0);
    // The camera stands behind player 1 (heading 0 faces +y: 4 m back along y, 2 m up), in RenderWare's axes.
    const coney::world::Vec3 feet = coney::platform::toRenderWare(world.humans().begin()->second.position);
    const coney::world::Vec3 eye = drawn.lastView().pose.position;
    const float dx = eye.x - feet.x;
    const float dy = eye.y - feet.y;
    const float dz = eye.z - feet.z;
    CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) < 5.0F);
    CHECK(dy > 1.0F);

    // The standing human leaves the host's game: once the client draws a step without him, his mesh goes.
    for (std::uint32_t step = 10; step < 20; ++step) {
        send(step, false);
        clientStep();
    }
    CHECK(drawn.humansPosed() == 1);
    CHECK(drawn.meshes() == 1);
    std::printf("client scene of level99: %zu log lines, %zu humans posed, %zu meshes\n", log.size(),
                drawn.humansPosed(), drawn.meshes());
}
