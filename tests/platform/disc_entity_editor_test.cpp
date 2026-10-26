// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the Entity editor (docs/guides/debug-menu.md#entity-editor) edits a level
// as it is played: in `level80` played in the game session as `coney --play-level level80 --checkpoint 2` plays it
// (support/disc_session.h), a human the level's scripts hold by handle is deleted through the editor and the level goes
// on with no handle left dangling, player 1 is refused with a reason and plays on, and an edited human (scaled, hurt,
// sped up, moved) takes its edits between steps. It runs only when the environment variable CONEY_DISC names the disc
// and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "debug/debug_session.h"
#include "debug/entity_edit.h"
#include "debug/entity_inspect.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

using Catch::Approx;
using coney::debug::EditOp;
using coney::debug::EntityCommand;
using coney::debug::EntityId;

namespace {

// How long anything here may take: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
                                   "180 tap cross\n210 tap cross\n240 tap cross\n270 tap cross\n300 tap cross\n"
                                   "330 tap cross\n360 tap cross\n390 tap cross\n420 tap cross\n450 tap cross\n"
                                   "480 tap cross\n510 tap cross\n540 tap cross\n570 tap cross\n600 tap cross\n";

// The inspector's id of the AI human at `index` (the first AI id, then the index).
constexpr EntityId kFirstAiId = 0x20000;

// The humans of `fighters` still in the world.
std::size_t liveCount(const coney::ai::AiHumans& fighters) {
    std::size_t live = 0;
    for (const coney::ai::AiHuman& entry : fighters.humans()) {
        live += entry.removed ? 0 : 1;
    }
    return live;
}

// The level80 session, started and run until the player has the pad, with the debug menus over it as `coney` wires
// them.
struct Level80 {
    explicit Level80(const coney::io::Wad& wad) : game(wad, kSkipScene) {
        game.session().startAtLevel("level80", 2);
        REQUIRE(game.runUntil(
            [this] { return game.session().play() != nullptr && game.session().play()->player().padControlled(); },
            kLimit));
        game.run(30);
        coney::platform::connectDebugServices(services, [this] { return &game.session(); });
        services.play = [this] { return controls.controlsOf(game.session().play()); };
        debug = std::make_unique<coney::debug::DebugSession>(tunables, services, nullptr);
        play = game.session().play();
        REQUIRE(play != nullptr);
    }
    // Runs `frames` more frames.
    void run(std::uint64_t frames) { game.run(frames); }
    // Applies the queued edits as the input gate does between two steps.
    std::size_t applyEdits() {
        return debug->entityEditor().applyPending(controls.controlsOf(game.session().play())->entityEdits());
    }
    coney::test::DiscSession game;
    coney::debug::DebugServices services;
    coney::platform::PlayLevelControls controls;
    coney::debug::TunableRegistry tunables;
    std::unique_ptr<coney::debug::DebugSession> debug;
    coney::platform::PlayLevelMode* play = nullptr;
};

} // namespace

// A raw removal leaves the scripts' handles on a freed human (#718); the editor's Delete is the game's own: the handle
// is unbound, the gang and formation let go, the human out of the world.
TEST_CASE("deleting a human the level's scripts hold goes the way a script's delete does, and the level goes on",
          "[disc][story][debug][entity-editor]") {
    // Expected: unverified, #723
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Level80 level(*wad);
    coney::ai::AiHumans& fighters = level.play->fighters();

    // A human the scripts made: one with a handle of its own, not the human the pad drives.
    std::optional<std::size_t> victim;
    double handle = 0.0;
    for (std::size_t i = 0; i < fighters.humans().size() && !victim; ++i) {
        const coney::ai::AiHuman& entry = fighters.humans()[i];
        const coney::ai::Brain* brain = fighters.brains().find(*entry.human);
        if (!entry.removed && !entry.debugSpawned && brain != nullptr && brain->handle() != 0.0 &&
            entry.human.get() != &fighters.playerBrain().human()) {
            victim = i;
            handle = brain->handle();
        }
    }
    REQUIRE(victim.has_value());
    const coney::human::Human* human = fighters.humans()[*victim].human.get();
    const std::size_t before = liveCount(fighters);
    const std::size_t brainsBefore = fighters.brains().size();

    // Queued, nothing happens; applied between steps, the human is on its way out.
    REQUIRE(level.debug->entityEditor().queue(EntityCommand{.entity = kFirstAiId + *victim, .op = EditOp::Delete}));
    CHECK(liveCount(fighters) == before);
    CHECK(level.applyEdits() == 1);
    // Out of the world at the end of a step.
    level.run(300);
    const bool removed = fighters.humans()[*victim].removed;
    const bool unbound = fighters.brains().find(*human) == nullptr;
    std::printf("entity editor: deleted script human %zu of %zu; removed %d, brain gone %d, brains %zu -> %zu\n",
                *victim, before, removed ? 1 : 0, unbound ? 1 : 0, brainsBefore, fighters.brains().size());
    CHECK(removed);
    CHECK(unbound);
    CHECK(liveCount(fighters) == before - 1);
    CHECK(fighters.brains().size() == brainsBefore - 1);
    CHECK(handle != 0.0);
    // The run log says what was done.
    REQUIRE_FALSE(level.debug->entityEditor().journal().empty());
    CHECK(level.debug->entityEditor().journal().back() ==
          std::string("entity edit: applied ") + std::to_string(kFirstAiId + *victim) + " delete");

    // It is no longer listed, and deleting it again is refused rather than crashing.
    CHECK(coney::debug::findEntity(level.controls.inspect()->entities(), kFirstAiId + *victim) == nullptr);
    REQUIRE(level.debug->entityEditor().queue(EntityCommand{.entity = kFirstAiId + *victim, .op = EditOp::Delete}));
    CHECK(level.applyEdits() == 0);
    CHECK(level.debug->entityEditor().journal().back().starts_with("entity edit: refused"));

    // The level's scripts run on over the humans they hold, and the player still has the pad.
    level.run(600);
    CHECK(liveCount(fighters) == before - 1);
    CHECK(level.play->player().padControlled());
}

TEST_CASE("player 1 is refused a delete with a reason and plays on", "[disc][story][debug][entity-editor]") {
    // Expected: unverified, #723 (the player-gone path is not ported: camera, HUD, scripts' HuIsAPlayer)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Level80 level(*wad);
    const std::size_t before = liveCount(level.play->fighters());
    REQUIRE(level.debug->entityEditor().queue(EntityCommand{.entity = 1, .op = EditOp::Delete}));
    CHECK(level.applyEdits() == 0);
    REQUIRE_FALSE(level.debug->entityEditor().journal().empty());
    const std::string line = level.debug->entityEditor().journal().back();
    std::printf("entity editor: player 1 delete -> %s\n", line.c_str());
    CHECK(line.starts_with("entity edit: refused 1 delete: player 1 cannot be deleted yet"));
    level.run(120);
    CHECK(level.play->player().padControlled());
    CHECK(level.play->player().human().alive());
    CHECK(liveCount(level.play->fighters()) == before);
    CHECK(level.controls.inspect()->entities().front().kind == coney::debug::EntityKind::Player);
}

TEST_CASE("edits to a human in a level land between steps and play goes on", "[disc][story][debug][entity-editor]") {
    // Expected: unverified, #723
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Level80 level(*wad);
    coney::ai::AiHumans& fighters = level.play->fighters();
    // Any AI human with a brain that is not the player's.
    std::optional<std::size_t> pick;
    for (std::size_t i = 0; i < fighters.humans().size() && !pick; ++i) {
        const coney::ai::AiHuman& entry = fighters.humans()[i];
        if (!entry.removed && entry.human.get() != &fighters.playerBrain().human() &&
            fighters.brains().find(*entry.human) != nullptr) {
            pick = i;
        }
    }
    REQUIRE(pick.has_value());
    coney::human::Human& human = *fighters.humans()[*pick].human;
    const EntityId id = kFirstAiId + *pick;
    const float scale = human.scale();
    const int maxHealth = human.health().maximum();
    coney::debug::EntityEditor& editor = level.debug->entityEditor();

    // The player's feet, 3 m to his right: a spot on the ground the level has.
    const coney::anim::Vec3 feet = level.play->playerFeet();
    editor.queue(EntityCommand{.entity = id, .op = EditOp::Scale, .values = {scale * 3.0, 0.0, 0.0}});
    editor.queue(EntityCommand{.entity = id, .op = EditOp::Health, .values = {50.0, 0.0, 0.0}});
    editor.queue(EntityCommand{.entity = id, .op = EditOp::SpeedScale, .values = {2.0, 0.0, 0.0}});
    editor.queue(EntityCommand{.entity = id, .op = EditOp::AnimRate, .values = {0.5, 0.0, 0.0}});
    editor.queue(EntityCommand{.entity = id, .op = EditOp::Invincible, .values = {1.0, 0.0, 0.0}});
    editor.queue(EntityCommand{.entity = id,
                               .op = EditOp::Teleport,
                               .values = {static_cast<double>(feet.x) + 3.0, static_cast<double>(feet.y), feet.z}});
    // Nothing moved yet.
    CHECK(human.scale() == Approx(scale));
    CHECK(human.health().value() != maxHealth / 2);
    CHECK(level.applyEdits() == 6);
    CHECK(human.scale() == Approx(scale * 3.0F));
    CHECK(human.meshScale() == Approx(3.0F));
    CHECK(human.health().value() == Approx(maxHealth / 2.0F).margin(1.0F));
    CHECK(human.speedScale() == Approx(2.0F));
    CHECK(human.animRate() == Approx(0.5F));
    CHECK(human.collisionScale() == Approx(scale * 3.0F));
    CHECK(human.position().x == Approx(feet.x + 3.0F).margin(0.1F));

    // Collision kept as configured, then a reset of everything with a configured value.
    editor.queue(EntityCommand{.entity = id, .op = EditOp::ScaleCollision, .values = {0.0, 0.0, 0.0}});
    CHECK(level.applyEdits() == 1);
    CHECK(human.collisionScale() == Approx(scale));
    CHECK(human.scale() == Approx(scale * 3.0F));
    for (std::size_t i = 0; i < coney::debug::kEditOpCount; ++i) {
        editor.queueReset(id, static_cast<EditOp>(i), level.controls);
    }
    CHECK(editor.pending() > 0);
    level.applyEdits();
    std::printf("entity editor: edited human %zu: scale %.2f -> %.2f -> reset %.2f, health %d of %d\n", *pick, scale,
                scale * 3.0F, human.scale(), human.health().value(), human.health().maximum());
    CHECK(human.scale() == Approx(scale));
    CHECK(human.meshScale() == Approx(1.0F));
    CHECK(human.collisionFollowsMesh());
    CHECK(human.speedScale() == Approx(1.0F));
    CHECK(human.animRate() == Approx(1.0F));
    CHECK(human.health().value() == human.health().maximum());

    // Play goes on over all of it.
    level.run(300);
    CHECK(level.play->player().padControlled());
    CHECK(level.play->player().human().alive());
}
