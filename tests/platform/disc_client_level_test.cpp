// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the client game's scene of `level99` (docs/guides/client-world.md#client-game)
// loads the level and the stand-in character, and draws the humans of a client's world, fed snapshots as a guest is,
// each posed from its layers with the character's clips, the camera on the host's player 1. Headless, on librw's NULL
// device. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts
// only (LEGAL.md).
#include <array>
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

#include "characters/character_list.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "hud/hud.h"
#include "hud/hud_owner.h"
#include "net/character_id.h"
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
        // A default frame with only the clock set (a designated initializer would leave members out).
        coney::hud::HudFrame hudFrame;
        hudFrame.nowMs = 100 * (index + 1);
        hudOwner.hud().update(hudFrame);
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

// Expected: unverified, #1612 (Coney's own: docs/guides/client-world.md#characters; the characters are the Character
// List's `warr_cl` (Cleon) and `warr_re_cv` (Rembrandt), the model names of docs/references/characters.md)
TEST_CASE("the client game's scene draws each human with the character his snapshot names",
          "[disc][net][client_game]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto list = coney::characters::loadCharacterList(*wad);
    REQUIRE(list.has_value());
    const auto cleonIndex = list->indexOf("warr_cl");
    const auto rembrandtIndex = list->indexOf("warr_re_cv");
    REQUIRE(cleonIndex.has_value());
    REQUIRE(rembrandtIndex.has_value());
    const std::uint32_t cleonHash = coney::characters::characterNameHash("warr_cl");
    const std::uint32_t rembrandtHash = coney::characters::characterNameHash("warr_re_cv");

    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    coney::hud::HudOwner hudOwner;
    auto scene =
        coney::platform::ClientLevelScene::load(**engine, *wad, {.level = "level99", .checkpoint = 1}, budget, hudOwner,
                                                [&log](std::string_view line) { log.emplace_back(line); });
    REQUIRE(scene.has_value());
    coney::platform::ClientLevelScene& drawn = **scene;

    // Four humans: Cleon, Rembrandt, one with no character named and one whose id is past the list's records.
    const auto id = [](std::uint16_t slot) { return coney::net::NetId{.slot = slot, .generation = 1}; };
    coney::net::SnapshotSender sender;
    coney::net::SnapshotReceiver receiver;
    coney::net::ClientWorld world;
    std::uint64_t index = 0;
    const auto run = [&](std::uint32_t from, std::uint32_t to, std::int32_t firstCharacter) {
        for (std::uint32_t step = from; step < to; ++step) {
            std::vector<coney::net::SnapshotHuman> humans;
            const std::array<std::int32_t, 4> characters{firstCharacter, coney::net::characterIdOf(*rembrandtIndex),
                                                         coney::net::kNoCharacter, coney::net::kMostCharacterId};
            // NOLINTNEXTLINE(bugprone-too-small-loop-variable): four slots
            for (std::uint16_t slot = 0; slot < characters.size(); ++slot) {
                coney::net::HumanState human = humanAt(3.0F * static_cast<float>(slot), 388, 0.0F);
                human.character = characters.at(slot);
                humans.push_back({.id = id(slot), .words = coney::net::humanWords(human), .priority = 1});
            }
            const coney::net::Snapshot snapshot = sender.build(step, humans);
            REQUIRE(receiver.apply(snapshot) == coney::net::SnapshotApplied::Applied);
            REQUIRE(sender.acknowledge(snapshot.sequence).has_value());
            world.receive(receiver);
            world.step();
            drawn.step(world, coney::FrameTime{.index = index++, .seconds = 1.0 / 30.0});
            drawn.render(world, coney::RenderTime{});
        }
    };
    const auto hashOf = [&drawn](coney::net::NetId human) -> std::optional<std::uint32_t> {
        const coney::human::PlayerCharacter* character = drawn.characterOf(human);
        return character != nullptr ? std::optional<std::uint32_t>(character->assets().record.nameHash) : std::nullopt;
    };

    run(0, 10, coney::net::characterIdOf(*cleonIndex));
    CHECK(drawn.humansPosed() == 4);
    CHECK(drawn.layersMissing() == 0);
    CHECK(hashOf(id(0)) == std::optional<std::uint32_t>{cleonHash});
    CHECK(hashOf(id(1)) == std::optional<std::uint32_t>{rembrandtHash});
    // No character named, or one that is not in the list: the fallback, Rembrandt.
    CHECK(hashOf(id(2)) == std::optional<std::uint32_t>{rembrandtHash});
    CHECK(hashOf(id(3)) == std::optional<std::uint32_t>{rembrandtHash});
    CHECK(drawn.characterOf(id(0)) != drawn.characterOf(id(1)));
    CHECK(drawn.meshes() == 4);
    CHECK(drawn.charactersLoaded() ==
          3); // the fallback, Cleon and Rembrandt by his index; the id past the list did not load

    // The first human takes Rembrandt's place: from the snapshot that says so, he is drawn as him.
    run(10, 20, coney::net::characterIdOf(*rembrandtIndex));
    CHECK(hashOf(id(0)) == std::optional<std::uint32_t>{rembrandtHash});
    CHECK(drawn.characterOf(id(0)) == drawn.characterOf(id(1)));
    std::printf("client scene characters: %zu humans posed, %zu meshes, %zu characters loaded\n", drawn.humansPosed(),
                drawn.meshes(), drawn.charactersLoaded());
}
